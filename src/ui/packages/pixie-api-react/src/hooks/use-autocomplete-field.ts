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

import {
  AUTOCOMPLETE_QUERIES,
  GQLAutocompleteEntityKind,
  GQLAutocompleteSuggestion,
} from '@pixie-labs/api';
import { useApolloClient } from '@apollo/client';
import * as React from 'react';

/**
 * Given a partial user input and a kind of entity to look for, suggests possible matches.
 *
 * Each suggestion is a GQLAutocompleteSuggestion, which offers:
 * - kind: What sort of entity is being suggested (pods, services, scripts, namespaces, etc)
 * - name: The name of the suggested entity
 * - description: If present, a human-readable description of that entity as opposed to others of its kind
 * - matchedIndices: If present, what part(s) of the input were used to find this suggestion
 * - state: If known, the status of the entity (what this can be depends on kind)
 */
export type AutocompleteFieldSuggester = (
  input: string, kind: GQLAutocompleteEntityKind
) => Promise<GQLAutocompleteSuggestion[]>;

/**
 * Given a cluster to check against, returns an @link{AutocompleteFieldSuggester} for that cluster.
 *
 * Usage:
 * ```
 * const getCompletions = useAutocompleteSuggestion('fooCluster');
 * React.useMemo(() => {
 *   // Might suggest the script `px/http_data` as a completion for `px/htt`
 *   getCompletions('htt', 'AEK_SCRIPT').then(suggestions => suggestions.map(s => doSomethingMeaningful(s)));
 * }, [getCompletions]);
 * ```
 */
export function useAutocompleteFieldSuggester(clusterUID: string): AutocompleteFieldSuggester {
  const client = useApolloClient();
  return React.useCallback<AutocompleteFieldSuggester>((partialInput: string, kind: GQLAutocompleteEntityKind) => (
    client.query<{ autocompleteField: GQLAutocompleteSuggestion[] }>({
      query: AUTOCOMPLETE_QUERIES.FIELD,
      fetchPolicy: 'network-only',
      variables: {
        input: partialInput,
        kind,
        clusterUID,
      },
    }).then(
      (results) => results.data.autocompleteField,
    )), [client, clusterUID]);
}
