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
import { CodeEditor, LazyPanel, ResizableDrawer } from '@pixie-labs/components';

import {
  createStyles, makeStyles, Theme, withStyles,
} from '@material-ui/core/styles';
import Tab from '@material-ui/core/Tab';
import Tabs from '@material-ui/core/Tabs';
import ChevronRight from '@material-ui/icons/ChevronRight';

import { getKeyMap } from 'containers/live/shortcuts';
import { LayoutContext } from 'context/layout-context';
import { ScriptContext } from 'context/script-context';

const useStyles = makeStyles((theme: Theme) => createStyles({
  root: {
    height: '100%',
    color: theme.palette.foreground.one,
    minWidth: 0,
    overflow: 'hidden',
    width: '100%',
  },
  rootPanel: {
    height: '100%',
    display: 'flex',
    flexDirection: 'column',
  },
  splits: {
    '& .gutter:hover': {
      cursor: 'col-resize',
    },
  },
  tabs: {
    display: 'flex',
    flexDirection: 'row',
    backgroundColor: theme.palette.background.three,
  },
  panel: {
    flex: 1,
    minHeight: 0,
  },
  editor: {
    height: '100%',
  },
  spinner: {
    position: 'absolute',
    top: '50%',
    left: '50%',
    transform: 'translate(-50%, -50%)',
  },
  closer: {
    cursor: 'pointer',
    display: 'flex',
    alignItems: 'center',
  },
}));

const shortcutKeys = Object.values(getKeyMap()).map((keyBinding) => keyBinding.sequence);

const VisEditor = ({ visible }: {visible: boolean}) => {
  const classes = useStyles();
  const { visJSON, setVisEditorText } = React.useContext(ScriptContext);

  const editorRef = React.createRef<CodeEditor>();
  // We useEffect instead of relying on the prop because of an issue where a cursor
  // in the field causes onChange to be triggered partway through, leading to a
  // partial state being set.
  React.useEffect(() => {
    if (!editorRef.current) {
      return;
    }

    editorRef.current.changeEditorValue(visJSON);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [visJSON]);

  return (
    <CodeEditor
      ref={editorRef}
      onChange={(code: string) => {
        setVisEditorText(code);
      }}
      className={classes.editor}
      visible={visible}
      spinnerClass={classes.spinner}
      shortcutKeys={shortcutKeys}
      language='json'
    />
  );
};

const ScriptEditor = ({ visible }: {visible: boolean}) => {
  const classes = useStyles();
  const { pxl, setPxlEditorText } = React.useContext(ScriptContext);
  const editorRef = React.createRef<CodeEditor>();
  // We useEffect instead of relying on the prop because of an issue where a cursor
  // in the field causes onChange to be triggered partway through, leading to a
  // partial state being set.
  // TODO(philkuz) need to update the props above so that we re-render the editor less often.
  React.useEffect(() => {
    if (!editorRef.current) {
      return;
    }
    editorRef.current.changeEditorValue(pxl);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [pxl]);

  return (
    <CodeEditor
      ref={editorRef}
      onChange={(code: string) => {
        setPxlEditorText(code);
      }}
      className={classes.editor}
      visible={visible}
      spinnerClass={classes.spinner}
      shortcutKeys={shortcutKeys}
      language='python'
    />
  );
};

const StyledTabs = withStyles((theme: Theme) => createStyles({
  root: {
    flex: 1,
  },
  indicator: {
    backgroundColor: theme.palette.foreground.one,
  },
}))(Tabs);

const StyledTab = withStyles((theme: Theme) => createStyles({
  root: {
    textTransform: 'none',
    '&:focus': {
      color: theme.palette.foreground.two,
    },
  },
}))(Tab);

const LiveViewEditor = ({ visible }: {visible: boolean}) => {
  const classes = useStyles();
  const [tab, setTab] = React.useState('pixie');
  const { setEditorPanelOpen, editorPanelOpen } = React.useContext(LayoutContext);
  const closeEditor = () => setEditorPanelOpen(false);

  return (
    <div className={classes.root}>
      <LazyPanel show={editorPanelOpen} className={classes.rootPanel}>
        <div className={classes.tabs}>
          <StyledTabs
            value={tab}
            onChange={(event, newTab) => setTab(newTab)}
          >
            <StyledTab value='pixie' label='PxL Script' />
            <StyledTab value='vis' label='Vis Spec' />
          </StyledTabs>
          <div className={classes.closer} onClick={closeEditor}>
            <ChevronRight />
          </div>
        </div>
        <LazyPanel className={classes.panel} show={tab === 'pixie'}>
          <ScriptEditor visible={visible && tab === 'pixie'} />
        </LazyPanel>
        <LazyPanel className={classes.panel} show={tab === 'vis'}>
          <VisEditor visible={visible && tab === 'vis'} />
        </LazyPanel>
      </LazyPanel>
    </div>
  );
};

export const EditorSplitPanel = (props) => {
  const { editorPanelOpen } = React.useContext(LayoutContext);

  return (
    <ResizableDrawer
      drawerDirection='right'
      initialSize={850}
      open={editorPanelOpen}
      otherContent={props.children}
      overlay
    >
      <LiveViewEditor visible={editorPanelOpen} />
    </ResizableDrawer>
  );
};
