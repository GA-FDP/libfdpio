#!/bin/bash

set -euxo pipefail

test -f $PREFIX/lib/libfdpio${SHLIB_EXT}
test -f $PREFIX/include/fdpio.h
test -f $PREFIX/bin/test_fdpio
test -d $PREFIX/etc/xrootd/client.plugins.d

export XRD_PLUGINCONFDIR=${PREFIX}/etc/xrootd/client.plugins.d 
export XRDCP_ALLOW_HTTP=true 
export XRD_CURLNUMTHREADS=1 
export XRD_PELICANUSEAUTHHEADERS=true

test_fdpio


