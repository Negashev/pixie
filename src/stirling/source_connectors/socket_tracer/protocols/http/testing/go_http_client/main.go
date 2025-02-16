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

// The intention to have matching http client in Go, is to mimic the typical setup of calling a restful service in Go.
// Obviously, we can use 'curl' to send a http request. But that would not be how typical Go client works.
package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"math/rand"
	"net/http"
	"net/url"
	"time"
)

var letterRunes = []rune("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")

func randStringRunes(n int) string {
	b := make([]rune, n)
	for i := range b {
		b[i] = letterRunes[rand.Intn(len(letterRunes))]
	}
	return string(b)
}

type helloReply struct {
	Greeter string `json:"greeter"`
}

func main() {
	address := flag.String("address", "localhost:50050", "Server end point.")
	reqType := flag.String("reqType", "get", "Type of request (get or post)")
	name := flag.String("name", "world", "The name to greet, for GET requests.")
	reqSize := flag.Int("reqSize", 128*1024, "The size of the request, for POST requests.")
	count := flag.Int("count", 1, "The count of requests to make.")

	flag.Parse()

	values := map[string]string{}
	values["name"] = "foo"
	values["data"] = randStringRunes(*reqSize)
	postBody, err := json.Marshal(values)
	if err != nil {
		log.Fatal(err)
	}

	for i := 0; i < *count; i++ {
		if *reqType == "get" {
			resp, err := http.Get("http://" + *address + "/sayhello?name=" + url.QueryEscape(*name))
			if err != nil {
				panic(err)
			}

			body, readErr := ioutil.ReadAll(resp.Body)
			resp.Body.Close()
			if readErr != nil {
				log.Fatal(readErr)
			}

			reply := helloReply{}
			jsonErr := json.Unmarshal(body, &reply)
			if jsonErr != nil {
				log.Fatal(jsonErr)
			}

			fmt.Println(reply.Greeter)
		} else if *reqType == "post" {
			resp, err := http.Post("http://"+*address+"/post", "application/json", bytes.NewBuffer(postBody))
			if err != nil {
				panic(err)
			}

			fmt.Println(resp.Body)
		} else {
			log.Fatal("Did not understand reqType")
		}
		time.Sleep(time.Second)
	}
}
