#!/bin/bash

# go to project root
PROJECT_ROOT="$(cd `dirname $0`/..; pwd -P)"
cd "$PROJECT_ROOT"

cd po

# Update from source code
intltool-update -m
intltool-update -p -g ansel

# Remove old translations
for f in *.po ; do
  echo "$f"
  msgmerge -U $f ansel.pot
done

# Report
intltool-update -g ansel -r
