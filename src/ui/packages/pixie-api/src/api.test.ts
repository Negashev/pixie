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

import fetch from 'cross-fetch';
import { PixieAPIClient } from './api';
import { CloudClient } from './cloud-gql-client';
import { mockApolloClient, Invocation } from './testing';
import { GQLAutocompleteActionType, GQLAutocompleteEntityKind } from './types/schema';
// Imported only so that its import in the test subject can be mocked successfully.
import * as vizierDependency from './vizier-grpc-client';

jest.mock('cross-fetch', () => ({
  default: jest.fn(),
}));

describe('Pixie TypeScript API Client', () => {
  mockApolloClient();
  jest.mock('./vizier-grpc-client');

  const mockFetchResponse = (response, reject = false) => {
    (fetch as jest.Mock).mockImplementation(
      () => (reject ? Promise.reject(response) : Promise.resolve(response)),
    );
  };

  it('can be instantiated', async () => {
    const client = await PixieAPIClient.create({ apiKey: '' });
    expect(client).toBeTruthy();
  });

  describe('authentication check', () => {
    it('treats HTTP 200 to mean successful authentication', async () => {
      mockFetchResponse({ status: 200 } as Response);
      const client = await PixieAPIClient.create({ apiKey: '' });
      const authenticated = await client.isAuthenticated();
      expect(authenticated).toBe(true);
    });

    it('treats any other HTTP status as not authenticated', async () => {
      mockFetchResponse({ status: 500 } as Response);
      const client = await PixieAPIClient.create({ apiKey: '' });
      const authenticated = await client.isAuthenticated();
      expect(authenticated).toBe(false);
    });

    it('treats a failed request as an error', async () => {
      mockFetchResponse('Ah, bugger.', true);
      const client = await PixieAPIClient.create({ apiKey: '' });
      try {
        const authenticated = await client.isAuthenticated();
        if (authenticated) fail('The fetch request rejected, but isAuthenticated came back with true.');
        fail('The fetch request rejected, but isAuthenticated didn\'t follow suit.');
      } catch (e) {
        expect(e).toBe('Ah, bugger.');
      }
    });
  });

  // The GQL methods are just proxies to CloudGQLClient, which has its own tests. Only skeletal tests needed here.

  const proxies: ReadonlyArray<Invocation<CloudClient>> = [
    ['createAPIKey'],
    ['listAPIKeys'],
    ['deleteAPIKey', 'foo'],
    ['createDeploymentKey'],
    ['listDeploymentKeys'],
    ['deleteDeploymentKey', 'foo'],
    ['createUserInvitation', 'Test', 'Exampleton', 'test@example.com'],
    ['listClusters'],
    ['getClusterControlPlanePods'],
    ['getSetting', 'tourSeen'],
    ['setSetting', 'tourSeen', true],
  ];

  it.each(proxies)('%s forwards to CloudClient', async (name: keyof CloudClient, ...args: any[]) => {
    const client = await PixieAPIClient.create({ apiKey: '' });
    const spy = spyOn(client.getCloudGQLClientForAdapterLibrary(), name);
    expect(typeof client[name]).toBe('function');
    client[name](...args);
    expect(spy).toHaveBeenCalledWith(...args);
  });

  describe('autocomplete methods', () => {
    it('getAutocompleteSuggester forwards to CloudClient', async () => {
      const client = await PixieAPIClient.create({ apiKey: '' });
      const spy = jest.fn();
      spyOn(client.getCloudGQLClientForAdapterLibrary(), 'getAutocompleteSuggester').and.returnValue(spy);
      const suggester = await client.getAutocompleteSuggester('foo');
      expect(typeof suggester).toBe('function');
      suggester('bar', 0, GQLAutocompleteActionType.AAT_SELECT);
      expect(spy).toHaveBeenCalledWith('bar', 0, GQLAutocompleteActionType.AAT_SELECT);
    });

    it('getAutocompleteFieldSuggester forwards to CloudClient', async () => {
      const client = await PixieAPIClient.create({ apiKey: '' });
      const spy = jest.fn();
      spyOn(client.getCloudGQLClientForAdapterLibrary(), 'getAutocompleteFieldSuggester').and.returnValue(spy);
      const suggester = await client.getAutocompleteFieldSuggester('foo');
      expect(typeof suggester).toBe('function');
      suggester('bar', GQLAutocompleteEntityKind.AEK_SCRIPT);
      expect(spy).toHaveBeenCalledWith('bar', GQLAutocompleteEntityKind.AEK_SCRIPT);
    });
  });

  describe('gRPC proxy methods', () => {
    it('connects to a cluster to request a health check', async () => {
      const spy = jest.fn(() => Promise.resolve('bar'));
      spyOn(vizierDependency, 'VizierGRPCClient').and.returnValue({ health: spy });

      const client = await PixieAPIClient.create({ apiKey: '' });
      spyOn(client.getCloudGQLClientForAdapterLibrary(), 'getClusterConnection').and.returnValue({});

      const out = await client.health('foo').toPromise();
      expect(spy).toHaveBeenCalled();
      expect(out).toBe('bar');
    });

    it('connects to a cluster to request a script execution', async () => {
      const spy = jest.fn(() => Promise.resolve('bar'));
      spyOn(vizierDependency, 'VizierGRPCClient').and.returnValue({ executeScript: spy });

      const client = await PixieAPIClient.create({ apiKey: '' });
      spyOn(client.getCloudGQLClientForAdapterLibrary(), 'getClusterConnection').and.returnValue({});

      const out = await client.executeScript('foo', 'import px').toPromise();
      expect(spy).toHaveBeenCalled();
      expect(out).toBe('bar');
    });
  });
});
