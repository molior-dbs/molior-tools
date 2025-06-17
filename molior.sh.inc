# vim: syntax=sh

if [ -e ~/.moliorrc ]; then
  . ~/.moliorrc
else
  echo -n "Please specify the molior server URL: "
  read MOLIOR_URL
  echo -n "Please specify the molior debian repository URL: "
  read MOLIOR_REPO_URL

  cat > ~/.moliorrc <<EOF
MOLIOR_URL=$MOLIOR_URL
MOLIOR_REPO_URL=$MOLIOR_REPO_URL
EOF
  if [ -n "$SUDO_USER" ]; then
    chown $SUDO_USER ~/.moliorrc
  fi
  echo "Configuration saved to ~/.moliorrc"
  echo "Please rerun $0 $@"
  exit 0
fi

if [ -z "$PUBKEY_FILE" ]; then
  PUBKEY_FILE=archive-keyring.asc
fi

aquire_user()
{
  if [ -z "$MOLIOR_USER" ]; then
    echo -n "Please specify the molior login user: "
    read MOLIOR_USER
    echo MOLIOR_USER=$MOLIOR_USER >> ~/.moliorrc
  fi
}

aquire_pass()
{
  if [ -z "$MOLIOR_PASSWORD" ]; then
    echo -n "Please specify the molior login password: "
    test -t 0 && stty -echo
    read MOLIOR_PASSWORD
    test -t 0 && stty echo
  fi
}

# Fix obsolete pxz package on buster
xzversion=`dpkg -s xz-utils | grep ^Version: | sed 's/^Version: //'`
if dpkg --compare-versions "$xzversion" lt 5.2.4-1; then
  TAR_PXZ="-Ipxz"
  TAR_EXTRA_OPTS=""
  PXZ_CMD=pxz
  PXZ_EXTRA_OPTS=""
else
  TAR_PXZ=""
  TAR_EXTRA_OPTS="J"
  PXZ_CMD=xz
  PXZ_EXTRA_OPTS=""
fi

