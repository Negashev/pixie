import * as React from 'react';

import SvgIcon, { SvgIconProps } from '@material-ui/core/SvgIcon';

export const UnexpandedIcon = (props: SvgIconProps) => (
  <SvgIcon
    {...props}
    width='24'
    height='24'
    viewBox='0 0 24 24'
    fill='none'
    xmlns='http://www.w3.org/2000/svg'
  >
    <g opacity='0.45'>
      <path
        d='M10 6L8.59 7.41L13.17 12L8.59 16.59L10 18L16 12L10 6Z'
        fill='white'
      />
    </g>
  </SvgIcon>
);
