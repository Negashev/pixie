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

import * as React from 'react';
import { PixienautForm, FormStructure } from '@pixie-labs/components';
import { RouteProps } from 'react-router';
import * as QueryString from 'query-string';
import { BasePage } from './base';
import { GetOAuthProvider } from './utils';

export const ErrorPage: React.FC = ({ location }: RouteProps) => {
  const parsed = QueryString.parse(location.search);
  const err = parsed.error as string;
  const [error, setError] = React.useState<Error>(null);
  const [method, setMethod] = React.useState<FormStructure>(null);

  React.useEffect(() => {
    GetOAuthProvider().getError().then((m) => {
      setMethod(m);
      setError(null);
    }).catch((e) => setError(e));
  }, [err]);

  return (
    <BasePage>
      {error && <div>{error.message}</div>}
      {method && (
        <PixienautForm formProps={method} />
      )}
    </BasePage>
  );
};
