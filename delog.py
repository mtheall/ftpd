#!/usr/bin/env python

import sys
import re

if __name__ == '__main__':
  regex = re.compile('\x1b\[[0-9]*;[0-9]*H')
  for line in sys.stdin:
    print(regex.sub('', line).strip())
