#!/bin/sh

ARCH=$(echo $1 | cut -d'-' -f1)

if echo "$ARCH" | grep -Eq '(^|[^A-Za-z0-9])(x86_64|i[[:digit:]]86)'; then
  echo x86
else
  exit 1
fi
