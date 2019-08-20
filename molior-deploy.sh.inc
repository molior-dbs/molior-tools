# vim: syntax=sh

PARTSEP=""
MAXPART=16

fs_has_partitions=0
fs_has_lvm=0
fs_has_luks=0

create_fstab()
{
  if [ -z "$target" ]; then
    log_error "Error: \$target not set"
  fi
  echo "# $PROJECT fstab" > $target/etc/fstab

  if [ -n "$ROOTDEVICE" ]; then
    if [ -z "$ROOTFS" ]; then
        ROOTFS=auto
    fi
    if [ -z "$ROOTFSOPTS" ]; then
      o="defaults,errors=remount-ro,noatime"
    else
      o=$ROOTFSOPTS
    fi
    echo "$ROOTDEVICE / $ROOTFS $o 0 1" >> $target/etc/fstab

  else
    for p in `seq 1 $MAXPART`
    do
      if [ -n "$SFDISK" ]; then
        eval t=\$PART${p}_MNT
        if [ "$t" = "/" ]; then
          eval s=\$PART${p}_FS
          if [ -z "$s" ]; then
            break
          fi
          eval o=\$PART${p}_OPTS
          if [ -z "$o" ]; then
            o="defaults,errors=remount-ro,noatime"
          fi
          part=${disk}$PARTSEP$p
          uuid=`blkid -o value -s UUID $part`
          if [ -z "$uuid" ]; then
            log_error "Error rootfs uuid is empty for '$part'"
          fi
          echo "UUID=$uuid $t $s $o 0 1" >> $target/etc/fstab
          break
        fi
      fi

      if [ -n "$LVM_VG" ]; then
        eval t=\$LVM_LV${p}_MNT
        if [ "$t" = "/" ]; then
          eval s=\$LVM_LV${p}_FS
          if [ -z "$s" ]; then
            break
          fi
          eval o=\$LVM_LV${p}_OPTS
          if [ -z "$o" ]; then
            o="defaults,errors=remount-ro,noatime"
          fi
          eval u=\$LVM_LV${p}_NAME
          if [ -z "$u" ]; then
            log_error "LVM no NAME"
          fi
          part=/dev/mapper/$(echo ${LVM_VG} | sed -r 's/-/--/g')-$u
          uuid=`blkid -o value -s UUID $part`
          if [ -z "$uuid" ]; then
            log_error "Error rootfs uuid is empty for '$part'"
          fi
          echo "UUID=$uuid $t $s $o 0 1" >> $target/etc/fstab
          break
        fi
      fi
    done
  fi

  if [ -n "$SFDISK" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval t=\$PART${p}_MNT
      if [ "$t" = "/" ]; then
        continue
      fi
      eval s=\$PART${p}_FS
      if [ -z "$s" ]; then
        break
      fi
      eval o=\$PART${p}_OPTS
      if [ -z "$o" ]; then
        o="defaults"
      fi
      part=${disk}$PARTSEP$p
      uuid=`blkid -o value -s UUID $part`
      if [ -z "$uuid" ]; then
        log_error "Error: empty uuid for '$part'"
      fi
      if [ "$s" = "swap" ]; then
        echo "UUID=$uuid none swap sw 0 0" >> $target/etc/fstab
      elif [ "$s" != "lvm" ]; then
        echo "UUID=$uuid $t $s $o 0 2" >> $target/etc/fstab
      fi
    done
  fi

  if [ -n "$LVM_VG" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval t=\$LVM_LV${p}_MNT
      if [ "$t" = "/" ]; then
        continue
      fi
      eval s=\$LVM_LV${p}_FS
      if [ -z "$s" ]; then
        break
      fi
      eval o=\$LVM_LV${p}_OPTS
      if [ -z "$o" ]; then
        o="defaults"
      fi
      eval u=\$LVM_LV${p}_NAME
      if [ -z "$u" ]; then
        log_error "LVM no FS"
      fi

      part=/dev/mapper/$(echo ${LVM_VG} | sed -r 's/-/--/g')-$u
      if [ "$s" != "encrypted-swap" ]; then
        uuid=`blkid -o value -s UUID $part`
        if [ -z "$uuid" ]; then
          log_error "Error: empty uuid for '$part'"
        fi
        if [ "$s" = "swap" ]; then
        echo "UUID=$uuid none swap sw 0 0" >> $target/etc/fstab
        else
          echo "UUID=$uuid $t $s $o 0 2" >> $target/etc/fstab
        fi
      else
        echo "/dev/mapper/${u}-open none swap sw 0 0" >> $target/etc/fstab
      fi

      eval c=\$LVM_LV${p}_CRYPTTAB
      if [ -n "$c" ]; then
        echo "${u}-open	${part}	$c" >> $target/etc/crypttab
      fi
    done
  fi

  if [ -n "$BIND_MOUNTS" ]; then
    echo >> $target/etc/fstab
    old_IFS=$IFS
    IFS=","
    for m in $BIND_MOUNTS
    do
      from=`echo $m | cut -d: -f1`
      to=`echo $m | cut -d: -f2`
      echo "$to $from none rw,noexec,nosuid,nodev,bind 0 0" >>$target/etc/fstab
    done
    IFS=$old_IFS
  fi

  if [ -n "$EXTRA_BIND_MOUNTS" ]; then
    echo >> $target/etc/fstab
    old_IFS=$IFS
    IFS=","
    for m in $EXTRA_BIND_MOUNTS
    do
      from=`echo $m | cut -d: -f1`
      to=`echo $m | cut -d: -f2`
      echo "$to $from none rw,noexec,nosuid,nodev,bind 0 0" >>$target/etc/fstab
    done
    IFS=$old_IFS
  fi
}

create_partitions()
{
  if [ -z "$disk" ]; then
    log_error "Error: \$disk not set"
  fi
  if [ -n "$SFDISK" ]; then
    echo " * creating partitions"
    echo "$SFDISK" | sfdisk --force $disk >/dev/null
  fi
}

create_lvm()
{
  part=$1
  if [ -z "$LVM_VG" ]; then
    log_error "Error: no LVM_VG specified"
  fi
  if vgs ${LVM_VG} >/dev/null 2>&1; then
    log_error "Error: LVM volume group $LVM_VG already exists. Please remove manually if it's not needed or choose different name"
    LVM_VG=""
  fi
  # 8MByte is good for SSDs and SD cards, and the current max
  pvcreate -f -y --dataalignment 8m $part >&2
  if [ $? -ne 0 ]; then
    log_error "Error creating LVM PV on $part"
  fi
  vgcreate -f -y ${LVM_VG} $part >&2 # FIXME: add PV if VG exists

  if [ $? -ne 0 ]; then
    log_error "Error creating LVM VG $LVM_VG on $part"
  fi

  for p in `seq 1 $MAXPART`
  do
    eval t=\$LVM_LV${p}_NAME
    if [ -z "$t" ]; then
      continue
    fi
    eval s=\$LVM_LV${p}_SIZE
    if [ -z "$s" ]; then
      log_error "LVM no size"
    fi
    if [ -n "${LVM_THINPOOL}" ]; then
      lvdisplay $LVM_VG/lv_thin >/dev/null 2>&1
      if [ $? -ne 0 ]; then
        lvcreate -y --extents "100%FREE" --thinpool lv_thin $LVM_VG
        if [ $? -ne 0 ]; then
          log_error "LVM thin pool create"
        fi
      fi
    fi
    echo " * creating LVM volume ${LVM_VG}-$t ($s)"
    if [ -n "${LVM_THINPOOL}" ]; then
      lvcreate -y --name $t --virtualsize $s --thin $LVM_VG/lv_thin >&2
    else
      lvcreate -y --name $t --size $s $LVM_VG >&2
    fi
    if [ $? -ne 0 ]; then
      log_error "Error creating LVM LV $t ($s) on $LVM_VG"
    fi
  done
}

create_luks()
{
  part=$1
  name=$2
  echo " * cryptsetup $part"
  if [ -z "$name" ]; then
    log_error "Error: LUKS partitions need a name. Set PART*_LUKSNAME or LVM_LV*_NAME"
  fi
  eval cipher=\$LUKS_${name}_CIPHER
  if [ -z "$cipher" ]; then
    cipher=aes-cbc-null
  fi
  eval keysize=\$LUKS_${name}_KEYSIZE
  if [ -z "$keysize" ]; then
    keysize=128
  fi
  eval passphrase=\$LUKS_${name}_PASSPHRASE
  if [ -z "$passphrase" ]; then
    passphrase="install"
  fi

  dd if=/dev/zero of=$part bs=4K count=1
  echo -n $passphrase | cryptsetup --cipher $cipher --key-size $keysize luksFormat $part -
  if [ $? -ne 0 ]; then
    log_error "Error formatting LUKS partition $part"
  fi
  echo -n $passphrase | cryptsetup luksOpen $part $name-decrypted
  if [ $? -ne 0 ]; then
    log_error "Error opening LUKS partition $part"
  fi
}

make_fs()
{
  s=$1
  part=$2
  name=$3
  opts=$4
  if [ "$s" = "encrypted-swap" ]; then
    # do nothing - will be created on startup
    true
  elif [ "$s" = "swap" ]; then
    echo " * mkswap $part"
    mkswap $part >/dev/null
    if [ $? -ne 0 ]; then
      log_error "Error formatting partition $part as $s"
    fi

  elif [ "$s" = "lvm" ]; then
    create_lvm $part

  elif [ "$s" = "luks" ]; then
    create_luks $part $name

  else # normal filesystems
    echo " * mkfs $part"
    eval mkfs $opts -t $s $part >/dev/null
    if [ $? -ne 0 ]; then
      log_error "Error formatting partition $part as $s"
    fi
  fi
}

create_fs()
{
  if [ -n "$SFDISK" ]; then
    for p in `seq 1 $MAXPART`
    do
      part=${disk}$PARTSEP$p
      eval s=\$PART${p}_FS
      if [ -z "$s" ]; then
        break
      fi
      eval t=\$PART${p}_MNT
      if [ "$t" = "/" ]; then
        rootpart=$part
      fi
      eval u=\$PART${p}_LUKSNAME
      eval opts=\$PART${p}_MKFSOPTS
      make_fs "$s" "$part" "$u" "$opts"
    done
  fi

  if [ -n "$LVM_VG" ]; then
    if [ "$LVM_PV" = "disk" ]; then
      create_lvm $disk
    fi
    for p in `seq 1 $MAXPART`
    do
      eval t=\$LVM_LV${p}_NAME
      if [ -z "$t" ]; then
        continue
      fi
      eval s=\$LVM_LV${p}_FS
      if [ -z "$s" ]; then
        break
      fi
      if [ "$s" = "encrypted-swap" ]; then
        continue
      fi

      # lvm replaces hyphens with double hypents in lvm name
      part=/dev/mapper/$(echo ${LVM_VG} | sed -r 's/-/--/g')-$t
      eval u=\$LVM_LV${p}_MNT
      if [ "$u" = "/" ]; then
        rootpart=$part
      fi
      eval opts=\$LVM_LV${p}_MKFSOPTS
      make_fs "$s" "$part" "$t" "$opts"

      if [ "$s" = "luks" ]; then
        eval v=\$LUKS_${t}_FS
        if [ -z "$v" ]; then
          continue
        fi
        make_fs "$v" "/dev/mapper/$t-decrypted" "" ""
      fi
    done
  fi
}

get_fsinfo()
{
  if [ -n "$SFDISK" ]; then
    fs_has_partitions=1
    for p in `seq 1 $MAXPART`
    do
      eval s=\$PART${p}_FS
      if [ "$s" = "luks" ]; then
        fs_has_luks=1
        break
      fi
    done
  fi

  if [ -n "$LVM_VG" ]; then
    fs_has_lvm=1
    for p in `seq 1 $MAXPART`
    do
      eval s=\$LVM_LV${p}_FS
      if [ "$s" = "luks" ]; then
        fs_has_luks=1
        break
      fi
    done
  fi
}

mount_fs()
{
  if [ -z "$target" ]; then
    log_error "Error: \$target not set"
  fi
  if [ -n "$rootpart" ]; then
    part=$rootpart
    echo " * mount /"
    mount $part $target
    if [ $? -ne 0 ]; then
      log_error "Error mounting filesystem $part to $target"
    fi
  fi

  if [ -n "$SFDISK" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval t=\$PART${p}_MNT
      if [ -z "$t" ]; then
        continue
      fi
      if [ "$t" = "/" ]; then
        continue
      fi
      part=${disk}$PARTSEP$p
      echo " * mount $t"
      mkdir -p $target/$t
      mount $part $target/$t
      if [ $? -ne 0 ]; then
        log_error "Error mounting filesystem $part to $target$t"
      fi
      # FIXME: mount luks
    done
  fi

  if [ -n "$LVM_VG" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval s=\$LVM_LV${p}_NAME
      if [ -z "$s" ]; then
        continue
      fi
      eval t=\$LVM_LV${p}_MNT
      if [ -z "$t" ]; then

        eval u=\$LVM_LV${p}_FS
        if [ "$u" = "encrypted-swap" ]; then
          continue
        elif [ "$u" = "luks" ]; then
          eval v=\$LUKS_${s}_MNT
          if [ -z "$v" ]; then
            continue
          fi
          echo " * mount $v"
          mkdir -p $target/$v
          mount /dev/mapper/$s-decrypted $target/$v
        fi
        continue
      fi
      if [ "$t" = "/" ]; then
        continue
      fi

      part=/dev/mapper/$(echo ${LVM_VG} | sed -r 's/-/--/g')-$s
      echo " * mount $t"
      mkdir -p $target/$t
      mount $part $target/$t
    done
  fi
}

unmount()
{
  if mountpoint -q "$1"; then
    echo " * umount $1"
    umount "$1"
    if [ $? -ne 0 ]; then
      log_error "Error umounting $1"
    fi
  fi
}

umount_fs()
{
  if [ -z "$target" ]; then
    return
  fi
  sync

  mountpoints=""
  if [ -n "$SFDISK" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval s=\$PART${p}_MNT
      if [ -n "$s" ]; then
        mountpoints="$mountpoints$target$s\n"
      fi
      # FIXME: umount luks
    done
  fi

  if [ -n "$LVM_VG" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval s=\$LVM_LV${p}_MNT
      if [ -n "$s" ]; then
        mountpoints="$mountpoints$target$s\n"
      fi
      eval t=\$LVM_LV${p}_FS
      if [ "$t" = "encrypted-swap" ]; then
        continue
      elif [ "$t" = "luks" ]; then
        eval u=\$LVM_LV${p}_NAME
        if [ -n "$u" ]; then
          eval v=\$LUKS_${u}_MNT
          if [ -z "$v" ]; then
            continue
          fi
          mountpoints="$mountpoints$target$v\n"
        fi
      fi
    done
  fi

  for m in `/bin/echo -e "$mountpoints" | sort -r`
  do
    umount $m
  done

  # close LUKS devices
  if [ -n "$SFDISK" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval t=\$PART${p}_FS
      if [ -z "$t" ]; then
        break
      fi
      if [ "$t" = "luks" ]; then
        eval u=\$PART${p}_LUKSNAME
        if [ -z "$u" ]; then
          continue
        fi
        cryptsetup luksClose /dev/mapper/$u-decrypted
      fi
    done
  fi

  if [ -n "$LVM_VG" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval s=\$LVM_LV${p}_FS
      if [ -z "$s" ]; then
        break
      fi
      if [ "$s" = "encrypted-swap" ]; then
        continue
      elif [ "$s" = "luks" ]; then
        eval t=\$LVM_LV${p}_NAME
        if [ -z "$t" ]; then
          continue
        fi
        cryptsetup luksClose /dev/mapper/$t-decrypted
      fi
    done
  fi
}

get_deploy_config()
{
  echo "MOLIOR_TOOLS_VERSION=\"$MOLIOR_TOOLS_VERSION\""
  echo "DEPLOYMENT_TIMESTAMP=\"$DEPLOYMENT_TIMESTAMP\""
  echo "PROJECT=\"$PROJECT\""
  echo "VERSION=\"$VERSION\""
  echo "REVISION=\"$REVISION\""
  echo "VARIANT=\"$VARIANT\""
  echo "SUITE=\"$SUITE\""
  echo "SFDISK=\"$SFDISK\""
  echo "LVM_VG=\"$LVM_VG\""
  echo "ROOTDEVICE=\"$ROOTDEVICE\""
  echo "ROOTFS=\"$ROOTFS\""
  echo "ROOTFSOPTS=\"$ROOTFSOPTS\""
  for p in `seq 1 $MAXPART`
  do
    eval s=\$PART${p}_FS
    test -n "$s" && echo "PART${p}_FS=\"$s\""
    eval s=\$PART${p}_MNT
    test -n "$s" && echo "PART${p}_MNT=\"$s\""
    eval s=\$PART${p}_OPTS
    test -n "$s" && echo "PART${p}_OPTS=\"$s\""
    eval s=\$PART${p}_MKFSOPTS
    test -n "$s" && echo "PART${p}_MKFSOPTS=\"$s\""

    eval s=\$PART${p}_LUKSNAME
    if [ -n "$s" ]; then
      echo "PART${p}_LUKSNAME=\"$s\""
      luks_name=$s
      eval s=\$LUKS_${luks_name}_FS
      test -n "$s" && echo "LUKS_${luks_name}_FS=\"$s\""
      eval s=\$LUKS_${luks_name}_MNT
      test -n "$s" && echo "LUKS_${luks_name}_MNT=\"$s\""
      # FIXME: LUKS_${luks_name}_MKFSOPTS
    fi

    eval s=\$LVM_LV${p}_FS
    test -n "$s" && echo "LVM_LV${p}_FS=\"$s\""
    lvm_fs=$s
    eval s=\$LVM_LV${p}_MNT
    test -n "$s" && echo "LVM_LV${p}_MNT=\"$s\""
    eval s=\$LVM_LV${p}_NAME
    test -n "$s" && echo "LVM_LV${p}_NAME=\"$s\""
    eval s=\$LVM_LV${p}_OPTS
    test -n "$s" && echo "LVM_LV${p}_OPTS=\"$s\""
    eval s=\$LVM_LV${p}_CRYPTTAB
    test -n "$s" && echo "LVM_LV${p}_CRYPTTAB=\"$s\""
    if [ "$lvm_fs" = "luks" ]; then
      luks_name=$s
      eval s=\$LUKS_${luks_name}_FS
      test -n "$s" && echo "LUKS_${luks_name}_FS=\"$s\""
      eval s=\$LUKS_${luks_name}_MNT
      test -n "$s" && echo "LUKS_${luks_name}_MNT=\"$s\""
    fi
    eval s=\$LVM_LV${p}_SIZE
    test -n "$s" && echo "LVM_LV${p}_SIZE=\"$s\""
    eval s=\$LVM_LV${p}_MKFSOPTS
    test -n "$s" && echo "LVM_LV${p}_MKFSOPTS=\"$s\""
  done
  echo "BIND_MOUNTS=\"${BIND_MOUNTS}\""
  echo "EXTRA_BIND_MOUNTS=\"${EXTRA_BIND_MOUNTS}\""

  if [ -n "$TARGET_HOSTNAME" ]; then
    echo "HOSTNAME=\"$TARGET_HOSTNAME\""
  fi
  echo "LINUX_CMDLINE=\"$LINUX_CMDLINE\""
  echo "INSTALLER_PASSWD_USERS=\"$INSTALLER_PASSWD_USERS\""
  echo "INSTALLER_ASK_HOSTNAME=\"$INSTALLER_ASK_HOSTNAME\""
  echo "INSTALLER_ASK_CONFIRM=\"$INSTALLER_ASK_CONFIRM\""
  echo "INSTALLER_ASK_REBOOT=\"$INSTALLER_ASK_REBOOT\""
}
