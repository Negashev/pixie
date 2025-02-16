/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package controller

import (
	"context"
	"errors"
	"time"

	"github.com/gofrs/uuid"
	"github.com/nats-io/nats.go"
	log "github.com/sirupsen/logrus"

	"px.dev/pixie/src/shared/cvmsgspb"
	"px.dev/pixie/src/utils"
	"px.dev/pixie/src/utils/pbutils"
	"px.dev/pixie/src/vizier/services/certmgr/certmgrenv"
	"px.dev/pixie/src/vizier/services/certmgr/certmgrpb"
	"px.dev/pixie/src/vizier/utils/messagebus"
)

// K8sAPI is responsible for handing k8s requests.
type K8sAPI interface {
	CreateTLSSecret(name string, key string, cert string) error
	GetPodNamesForService(name string) ([]string, error)
	DeletePod(name string) error
}

// Server is an implementation of GRPC server for certmgr service.
type Server struct {
	env       certmgrenv.CertMgrEnv
	clusterID uuid.UUID
	k8sAPI    K8sAPI
	nc        *nats.Conn
	done      chan bool
}

// NewServer creates a new GRPC certmgr server.
func NewServer(env certmgrenv.CertMgrEnv, clusterID uuid.UUID, nc *nats.Conn, k8sAPI K8sAPI) *Server {
	return &Server{
		env:       env,
		clusterID: clusterID,
		nc:        nc,
		k8sAPI:    k8sAPI,
		done:      make(chan bool),
	}
}

// UpdateCerts updates the proxy certs with the given DNS address.
func (s *Server) UpdateCerts(ctx context.Context, req *certmgrpb.UpdateCertsRequest) (*certmgrpb.UpdateCertsResponse, error) {
	// Load secrets.
	err := s.k8sAPI.CreateTLSSecret("proxy-tls-certs", req.Key, req.Cert)
	if err != nil {
		return nil, err
	}

	// Bounce proxy service.
	pods, err := s.k8sAPI.GetPodNamesForService("vizier-proxy-service")
	if err != nil {
		return nil, err
	}

	if len(pods) == 0 {
		return nil, errors.New("No pods exist for proxy service")
	}

	for _, pod := range pods {
		err = s.k8sAPI.DeletePod(pod)

		if err != nil {
			return nil, err
		}
	}

	return &certmgrpb.UpdateCertsResponse{
		OK: true,
	}, nil
}

func (s *Server) sendSSLCertRequest() error {
	// Send over a request for SSL certs.
	regReq := &cvmsgspb.VizierSSLCertRequest{
		VizierID: utils.ProtoFromUUID(s.clusterID),
	}

	regReqAny, err := pbutils.MarshalAny(regReq)
	if err != nil {
		return err
	}

	c2vMsg := &cvmsgspb.V2CMessage{
		Msg: regReqAny,
	}

	b, err := c2vMsg.Marshal()
	if err != nil {
		return err
	}

	return s.nc.Publish(messagebus.V2CTopic("ssl"), b)
}

// CertRequester is a routine to go loop through cert requests. It's should be run in a go routine.
func (s *Server) CertRequester() {
	log.Info("Requesting SSL certs")
	sslCh := make(chan *nats.Msg)
	sub, err := s.nc.ChanSubscribe(messagebus.C2VTopic("sslResp"), sslCh)
	if err != nil {
		log.WithError(err).Warn("Failed to subscribe to sslResp channel")
	}
	defer func() {
		err := sub.Unsubscribe()
		if err != nil {
			log.WithError(err).Error("Failed to unsubscribe from sslResp channel")
		}
	}()

	configCh := make(chan *nats.Msg)
	sub, err = s.nc.ChanSubscribe(messagebus.C2VTopic("sslVizierConfigResp"), configCh)
	if err != nil {
		log.WithError(err).Warn("Failed to subscribe to sslVizierConfigResp channel")
	}
	defer func() {
		err := sub.Unsubscribe()
		if err != nil {
			log.WithError(err).Error("Failed to unsubscribe from sslVizierConfigResp channel")
		}
	}()

	err = s.sendSSLCertRequest()
	if err != nil {
		log.WithError(err).Warn("Failed to send message to request SSL certs")
	}

	t := time.NewTicker(30 * time.Second)
	defer t.Stop()

	sslResp := cvmsgspb.VizierSSLCertResponse{}
	vizConf := cvmsgspb.VizierConfig{}

	for {
		select {
		case <-s.done:
			return
		case <-t.C:
			log.Info("Timeout waiting for SSL certs. Re-requesting")
			err = s.sendSSLCertRequest()
			if err != nil {
				log.WithError(err).Warn("Failed to send message to request SSL certs")
			}
		case confMsg := <-configCh:
			log.Info("Got Vizier Config message")
			envelope := &cvmsgspb.C2VMessage{}
			err := envelope.Unmarshal(confMsg.Data)
			if err != nil {
				log.WithError(err).Error("Got bad Vizier Config")
				break
			}

			err = pbutils.UnmarshalAny(envelope.Msg, &vizConf)
			if err != nil {
				log.WithError(err).Error("Got bad Vizier Config")
				break
			}
			if vizConf.GetPassthroughEnabled() {
				// Reset timer to a longer duration since we don't need
				// to do anything in passthrough mode.
				// If the mode changes, we should get a message on the
				// config channel.
				t.Reset(1 * time.Hour)
			} else {
				t.Reset(30 * time.Second)
				err = s.sendSSLCertRequest()
				if err != nil {
					log.WithError(err).Warn("Failed to send message to request SSL certs")
				}
			}
		case sslMsg := <-sslCh:
			log.Info("Got SSL message")
			envelope := &cvmsgspb.C2VMessage{}
			err := envelope.Unmarshal(sslMsg.Data)
			if err != nil {
				// jump out and wait for timeout.
				log.WithError(err).Error("Got bad SSL response")
				break
			}

			err = pbutils.UnmarshalAny(envelope.Msg, &sslResp)
			if err != nil {
				log.WithError(err).Error("Got bad SSL response")
				break
			}

			certMgrReq := &certmgrpb.UpdateCertsRequest{
				Key:  sslResp.Key,
				Cert: sslResp.Cert,
			}

			ctx := context.Background()
			certMgrResp, err := s.UpdateCerts(ctx, certMgrReq)
			if err != nil {
				log.WithError(err).Fatal("Failed to update certs")
			}

			if !certMgrResp.OK {
				log.Fatal("Failed to update certs")
			}
			log.WithField("reply", certMgrResp.String()).Info("Certs Updated")

			t.Reset(5 * time.Minute)
		}
	}
}

// StopCertRequester stops the cert requester.
func (s *Server) StopCertRequester() {
	close(s.done)
}
