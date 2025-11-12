#!/bin/bash

set -euxo pipefail

# Note - This assumes that BEARER_TOKEN is set in the build environment
# (properly passed through meta.yaml)


test -d $PREFIX/etc/xrootd/client.plugins.d
test -f $PREFIX/lib/libXrdClPelican-5.so

export XRD_PLUGINCONFDIR=${PREFIX}/etc/xrootd/client.plugins.d 
test -d $XRD_PLUGINCONFDIR

export XRDCP_ALLOW_HTTP=true 
export XRD_CURLNUMTHREADS=1 
export XRD_PELICANUSEAUTHHEADERS=true

export XRD_LOGLEVEL=Debug

test_fdpio


