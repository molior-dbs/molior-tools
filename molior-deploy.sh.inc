# vim: syntax=sh

MAXPART=16

fs_has_partitions=0
fs_has_lvm=0
fs_has_luks=0
fs_on_raid=0
fs_has_btrfs=0
btrfs_root_part=
btrfs_uuid=""


exit_on_error()
{
  if [ "$?" -ne 0 ]; then
    log_error "$@"
  fi
}

create_fstab()
{
  log "creating fstab"
  if [ -z "$target" ]; then
    log_error "Error: \$target not set"
  fi
  echo "# $PROJECT fstab" > $target/etc/fstab

  # put root mount first to fstab (search in ROOTDEVICE, partitions, lvm)
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
    set +x
    for d in "" "DISK1_" "DISK2_" "DISK3_"
    do
      if [ -z "$d" ]; then
          eval t_type="\$DISK0_TYPE"
      else
          eval t_type="\$${d}TYPE"
      fi

      found=0
      for p in `seq 1 $MAXPART`
      do
        part_id=$p
        p="PART${p}_"

        # for raw disks without partitions, do not use PART_ in variable names
        if [ "$t_type" = "raw" ]; then
            p=""
        fi

        eval t_disk=\$${d}DISK
        if [ -z "$t_disk" ]; then
          t_disk=$disk
        fi
        eval t_mnt=\$${d}${p}MNT
        if [ -n "$t_mnt" ]; then
          if [ "$t_mnt" = "/" ]; then  # only include root partition
            eval t_fs=\$${d}${p}FS
            if [ -z "$t_fs" ]; then
              break
            fi
            eval o=\$${d}${p}OPTS
            if [ -z "$o" ]; then
              o="defaults,errors=remount-ro,noatime"
            fi

            if [ "$t_type" = "raw" ]; then
              part=$t_disk
              if [ ! -b "$part" ]; then
                break  # stop looping over partitions if device does not exist (eg optional RAID /dev/md0)
              fi
            else
              t_partsep=""
              if echo "$t_disk" | grep -q "[0-9]$"; then
                t_partsep="p"
              fi
              part=$t_disk$t_partsep$part_id
            fi

            uuid=`blkid -o value -s UUID $part`
            if [ -z "$uuid" ]; then
              log_error "Error rootfs uuid is empty for '$part'"
            fi
            set -x
            echo "UUID=$uuid $t_mnt $t_fs $o 0 1" >> $target/etc/fstab
            set +x
            found=1
            break
          fi
        fi

        # if raw disk, stop looping over partitions
        if [ "$t_type" = "raw" ]; then
            break
        fi
      done

      # create btrfs subvolume entries
      for p in `seq 1 $MAXPART`
      do
        eval subvol_path=\$${d}BTRFS_SUBVOL${p}_PATH
        if [ -z "$subvol_path" ]; then
         continue
        fi
        eval t_opts=\$${d}BTRFS_SUBVOL${p}_OPTS
        if [ -n "$t_opts" ]; then
          t_opts=",$t_opts"
        fi
        t_opts="subvol=/root$t_opts"
        echo "UUID=$btrfs_uuid $subvol_path btrfs $t_opts 0 0" >> $target/etc/fstab
        found=1
        break
      done

      if [ $found -eq 1 ]; then
        break
      fi
    done

    for p in `seq 1 $MAXPART`
    do
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
          part=/dev/mapper/$(echo ${LVM_VG} | sed -r 's/-/--/g')-$(echo $u | sed -r 's/-/--/g')
          uuid=`blkid -o value -s UUID $part`
          if [ -z "$uuid" ]; then
            log_error "Error rootfs uuid is empty for '$part'"
          fi
          set -x
          echo "UUID=$uuid $t $s $o 0 1" >> $target/etc/fstab
          set +x
          break
        fi
      fi
    done
    set -x
  fi

  # put all other entries to fstab
  entries=""
  set +x
  for d in "" "DISK1_" "DISK2_" "DISK3_"
  do
    if [ -z "$d" ]; then
        eval t_type="\$DISK0_TYPE"
    else
        eval t_type="\$${d}TYPE"
    fi

    for p in `seq 1 $MAXPART`
    do
      part_id=$p
      p="PART${p}_"

      # for raw disks without partitions, do not use PART_ in variable names
      if [ "$t_type" = "raw" ]; then
          p=""
      fi

      eval t_disk=\$${d}DISK
      if [ -z "$t_disk" ]; then
        t_disk=$disk
      fi
      eval t_fs=\$${d}${p}FS
      if [ -n "$t_fs" ]; then
        eval t_mnt=\$${d}${p}MNT
        if [ "$t_mnt" = "/" ]; then  # skip root partition
          continue
        fi
        if [ -z "$t_mnt" -a "$t_fs" != "swap" ]; then
          continue
        fi
        eval t_opts=\$${d}${p}OPTS
        if [ -z "$t_opts" ]; then
          t_opts="defaults"
        fi

        if [ "$t_type" = "raw" ]; then
          part=$t_disk
          if [ ! -b "$part" ]; then
            break  # stop looping over partitions if device does not exist (eg optional RAID /dev/md0)
          fi
        else
          t_partsep=""
          if echo "$t_disk" | grep -q "[0-9]$"; then
            t_partsep="p"
          fi
          part=$t_disk$t_partsep$part_id
        fi

        uuid=`blkid -o value -s UUID $part`
        if [ -z "$uuid" ]; then
          log_error "Error: empty uuid for '$part'"
        fi
        set -x
        if [ "$t_fs" = "swap" ]; then  # write swap directly
          echo "UUID=$uuid none swap sw 0 0" >> $target/etc/fstab
        elif [ "$t_fs" != "lvm" ]; then
          # remember for sorting
          entries="$entries\nUUID=$uuid $t_mnt $t_fs $t_opts 0 2"
        fi
        set +x
      fi

      # if raw disk, stop looping over partitions
      if [ "$t_type" = "raw" ]; then
          break
      fi
    done

    # create btrfs subvolume entries
    for p in `seq 1 $MAXPART`
    do
      eval subvol_path=\$${d}BTRFS_SUBVOL${p}_PATH
      if [ -z "$subvol_path" ]; then
       continue
      fi
      if [ "$subvol_path" = "/" ]; then  # skip root partition
        continue
      fi
      eval t_opts=\$${d}BTRFS_SUBVOL${p}_OPTS
      if [ -n "$t_opts" ]; then
        t_opts=",$t_opts"
      fi
      t_opts="subvol=$subvol_path$t_opts"
      entries="$entries\nUUID=$btrfs_uuid $subvol_path btrfs $t_opts 0 0"
      if [ "$subvol_path" = "/swap" ]; then
        entries="$entries\n/swap/swapfile none swap defaults 0 0"
      fi
    done
  done
  set -x

  if [ -n "$LVM_VG" ]; then
    set +x
    for p in `seq 1 $MAXPART`
    do
      eval t=\$LVM_LV${p}_MNT
      if [ "$t" = "/" ]; then
        continue
      fi
      eval s=\$LVM_LV${p}_FS
      if [ -z "$t" ] && [ "$s" != "swap" -a "$s" != "encrypted-swap" ]; then
        continue
      fi
      if [ -z "$s" ]; then
        continue
      fi
      eval o=\$LVM_LV${p}_OPTS
      if [ -z "$o" ]; then
        o="defaults"
      fi
      eval u=\$LVM_LV${p}_NAME
      if [ -z "$u" ]; then
        log_error "LVM no FS"
      fi

      part=/dev/mapper/$(echo ${LVM_VG} | sed -r 's/-/--/g')-$(echo $u | sed -r 's/-/--/g')
      if [ "$s" != "encrypted-swap" ]; then
        uuid=`blkid -o value -s UUID $part`
        if [ -z "$uuid" ]; then
          log_error "Error: empty uuid for '$part'"
        fi
        set -x
        if [ "$s" = "swap" ]; then  # write swap directly
          echo "UUID=$uuid none swap sw 0 0" >> $target/etc/fstab
        else
          # remember for sorting
          entries="$entries\nUUID=$uuid $t $s $o 0 2"
        fi
        set +x
      else
        set -x
        echo "/dev/mapper/${u}-open none swap sw 0 0" >> $target/etc/fstab
        set +x
      fi

      eval c="\$LVM_LV${p}_CRYPTTAB"
      if [ -n "$c" ]; then
        set -x
        echo "${u}-open	${part}	$c" >> $target/etc/crypttab
        set +x
      fi
    done
    set -x
  fi

  # write sorted entries
  /bin/echo -e "$entries" | sort -k 2 >> $target/etc/fstab

  if [ -n "$BIND_MOUNTS" ]; then
    echo >> $target/etc/fstab
    for m in $BIND_MOUNTS
    do
      from=`echo $m | cut -d: -f1`
      to=`echo $m | cut -d: -f2`
      opts=`echo $m | cut -d: -f3`
      if [ -z "$opts" ]; then
        opts="rw,noexec,nosuid,nodev"
      fi
      echo "$from $to none $opts,bind 0 0" >>$target/etc/fstab
    done
  fi

  if [ -n "$EXTRA_BIND_MOUNTS" ]; then
    echo >> $target/etc/fstab
    for m in $EXTRA_BIND_MOUNTS
    do
      from=`echo $m | cut -d: -f1`
      to=`echo $m | cut -d: -f2`
      opts=`echo $m | cut -d: -f3`
      if [ -z "$opts" ]; then
        opts="rw,noexec,nosuid,nodevice,bind"
      elif [ -n "$opts" -a "$opts" = "defaults" ]; then
        opts="defaults,bind"
      else
        opts=$opts
      fi
      echo "$from $to none $opts 0 0" >>$target/etc/fstab
    done
  fi

  if [ -n "$RAID_NAME" ] || [ -n "$fs_on_raid" ] && [ "$fs_on_raid" -eq 1 ] ;then
    # store raid configuration
    mkdir -p $target/etc/mdadm
    mdadm --detail --scan >> $target/etc/mdadm/mdadm.conf
  fi
}

create_partitions()
{
  if [ -n "$RAID_NAME" ]; then
    log "creating raid $RAID_NAME"
    missing_device=
    if [ "$raid_add_missing_device" -eq 1 ]; then
      missing_device=missing
    fi
    echo y | mdadm --create --verbose $RAID_NAME $RAID_OPTS $RAID_DEVICES $missing_device
    disk=`mdadm --detail --scan | awk {'print $2'}`  # set disk as raid virtual disk
  fi

  if [ -z "$disk" ]; then
    log_error "Error: \$disk not set"
  fi

  sfdiskversion=`sfdisk -v | cut -d ' ' -f4`
  sfdiskmajor=`echo $sfdiskversion | cut -d. -f1`
  sfdiskminor=`echo $sfdiskversion | cut -d. -f2`
  wipearg=""
  if [ "$sfdiskmajor" -ge 2 -a "$sfdiskminor" -gt 27 ]; then
    wipearg="--wipe=always"
  fi

  for d in "" "DISK1_" "DISK2_" "DISK3_"
  do
    if [ -z "$d" ]; then
        eval t_type="\$DISK0_TYPE"
    else
        eval t_type="\$${d}TYPE"
    fi
    if [ -z "$t_type" ] || [ "$t_type" = "part" ]; then
        eval t_sfdisk="\$${d}SFDISK"
        if [ -n "$t_sfdisk" ]; then
          if [ -z "${d}" ]; then
            t_disk=$disk
          else
            eval t_disk="\$${d}DISK"
          fi

          log "creating partitions on $t_disk"
          # check if sfdisk supports wipearg, i.e. > 2.27.1
          echo "$t_sfdisk" | sfdisk --force $wipearg $t_disk >/dev/null
          exit_on_error "Error creating partitions"
        fi
    elif [ "$t_type" = "raid" ]; then
        eval t_raidname="\$${d}RAIDNAME"
        t_raid_id=`echo $t_raidname | tr a-z A-Z`
        eval t_raid_level="\$RAID_${t_raid_id}_LEVEL"
        eval t_raid_disks="\$RAID_${t_raid_id}_DISKS"
        eval t_raid_metadata="\$RAID_${t_raid_id}_METADATA"

        log "creating RAID $t_raidname (level: $t_raid_level)"

        # if requested, find all disks which are not install disk of source install medium; exclude virtual media with 0 size
        if [ "$t_raid_disks" = "remaining" ]; then
            t_raid_disks=`lsblk -bdn -o PATH,TYPE,SIZE | grep -v ^$disk | grep -w disk | grep -v -w 0 | awk {'print $1'}`
            if [ -n "$SOURCEDISK" ]; then
                t_raid_disks=`echo "$t_raid_disks" | grep -v $SOURCEDISK || true`
            fi
        fi

        # add metadata args if needed
        t_args=""
        if [ -n "$t_raid_metadata" ]; then
            t_args="--metadata=$t_raid_metadata"
        fi

        # count number of disks
        i=0
        for d in $t_raid_disks; do
          i=$((i+1))
        done

        if [ $i -gt 0 ]; then
            # create RAID
            wipefs -af $t_raid_disks >/dev/null 2>&1
            echo y | mdadm --create /dev/$t_raidname --verbose --level=$t_raid_level $t_args --raid-devices=$i $t_raid_disks
        fi
    fi
  done
}

create_lvm()
{
  part=$1
  if [ -z "$LVM_VG" ]; then
    log_error "Error: no LVM_VG specified"
  fi
  if vgs ${LVM_VG} >/dev/null 2>&1; then
    log_error "Error: LVM volume group $LVM_VG already exists. Please remove manually if it's not needed or choose different name"
  fi
  # 8MByte is good for SSDs and SD cards, and the current max
  pvcreate -f -y --dataalignment 8m $part >&2
  exit_on_error "Error creating LVM PV on $part"

  vgcreate -f -y ${LVM_VG} $part >&2 # FIXME: add PV if VG exists
  exit_on_error "Error creating LVM VG $LVM_VG on $part"

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
        exit_on_error "LVM thin pool create"
      fi
    fi
    log "creating LVM volume ${LVM_VG}-$t ($s)"
    if [ -n "${LVM_THINPOOL}" ]; then
      lvcreate -y --name $t --virtualsize $s --thin $LVM_VG/lv_thin >&2
    else
      lvcreate -y --name $t --size $s $LVM_VG >&2
    fi
    exit_on_error "Error creating LVM LV $t ($s) on $LVM_VG"
  done
}

create_luks()
{
  part=$1
  name=$2
  log "cryptsetup $part"
  if [ -z "$name" ]; then
    log_error "Error: LUKS partitions need a name. Set PART*_LUKSNAME or LVM_LV*_NAME"
  fi
  eval cipher=\$LUKS_${name}_CIPHER
  if [ -z "$cipher" ]; then
    cipher=aes-xts-plain64
  fi
  eval keysize=\$LUKS_${name}_KEYSIZE
  if [ -z "$keysize" ]; then
    keysize=256
  fi
  eval passphrase=\$LUKS_${name}_PASSPHRASE
  if [ -z "$passphrase" ]; then
    passphrase="install"
  fi

  wipefs -af $part >/dev/null 2>&1
  echo -n $passphrase | cryptsetup --cipher $cipher --key-size $keysize luksFormat $part - >&2
  exit_on_error "Error formatting LUKS partition $part"

  echo -n $passphrase | cryptsetup luksOpen $part $name-decrypted >&2
  exit_on_error "Error opening LUKS partition $part"
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
  elif [ "$s" = "bios" ]; then
    # wipe all data, grub will use it
    dd if=/dev/zero of=$part bs=4K count=8
  elif [ "$s" = "swap" ]; then
    log "mkswap $part"
    mkswap $part >/dev/null
    exit_on_error "Error formatting partition $part as $s"

  elif [ "$s" = "lvm" ]; then
    create_lvm $part

  elif [ "$s" = "luks" ]; then
    create_luks $part $name

  elif [ "$s" = "btrfs" ]; then
    log "mkfs $part"
    eval mkfs -t $s -f $opts $part >/dev/null
    exit_on_error "Error formatting partition $part as $s"
    mkdir -p $WORK_DIR/btrfs-root
    mount $part $WORK_DIR/btrfs-root
    # remember for grub
    btrfs_root_part=$part
    # remember for subvolumes
    uuid=`blkid -o value -s UUID $part`
    if [ -z "$uuid" ]; then
      log_error "Error: empty uuid for '$part'"
    fi
    btrfs_uuid=$uuid

  else # normal filesystems
    log "mkfs $part"
    eval mkfs -t $s $opts $part >/dev/null
    exit_on_error "Error formatting partition $part as $s"
  fi
}

create_fs()
{
  for d in "" "DISK1_" "DISK2_" "DISK3_"
  do
    if [ -z "$d" ]; then
        eval t_type="\$DISK0_TYPE"
    else
        eval t_type="\$${d}TYPE"
    fi

    for p in `seq 1 $MAXPART`
    do
      part_id=$p
      p="PART${p}_"

      # for raw disks without partitions, do not use PART_ in variable names
      if [ "$t_type" = "raw" ]; then
          p=""
      fi

      eval t_disk=\$${d}DISK
      if [ -z "$t_disk" ]; then
        t_disk=$disk
      fi
      eval t_fs=\$${d}${p}FS
      if [ -n "$t_fs" ]; then

        if [ "$t_type" = "raw" ]; then
          part=$t_disk
          if [ ! -b "$part" ]; then
             break  # stop looping over partitions if device does not exist (eg optional RAID /dev/md0)
          fi
        else
          t_partsep=""
          if echo "$t_disk" | grep -q "[0-9]$"; then
            t_partsep="p"
          fi
          part=$t_disk$t_partsep$part_id
        fi

        eval t=\$${d}${p}MNT
        if [ "$t" = "/" ]; then
          rootpart=$part
        fi

        eval u=\$${d}${p}LUKSNAME
        eval opts=\$${d}${p}MKFSOPTS
        make_fs "$t_fs" "$part" "$u" "$opts"

        if [ "$t_fs" = "luks" ]; then
          eval v=\$LUKS_${u}_FS
          if [ "$v" = "btrfs" ]; then  # support btrfs on luks
            set -x
            make_fs "$v" "/dev/mapper/$u-decrypted" "" ""
            set +x
          fi
        fi

      fi

      # if raw disk, stop looping over partitions
      if [ "$t_type" = "raw" ]; then
          break
      fi

    done
  done

  if [ -n "$LVM_VG" ]; then
    if [ -n "$LVM_PV" ]; then
      if [ "$LVM_PV" = "disk" ]; then
        create_lvm $disk
      else
        create_lvm $LVM_PV
      fi
    fi
    set +x
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
      part=/dev/mapper/$(echo ${LVM_VG} | sed -r 's/-/--/g')-$(echo $t | sed -r 's/-/--/g')
      eval u=\$LVM_LV${p}_MNT
      if [ "$u" = "/" ]; then
        rootpart=$part
      fi
      eval opts=\$LVM_LV${p}_MKFSOPTS
      set -x
      make_fs "$s" "$part" "$t" "$opts"
      set +x

      if [ "$s" = "luks" ]; then
        eval v=\$LUKS_${t}_FS
        if [ -z "$v" ]; then
          continue
        fi
        set -x
        make_fs "$v" "/dev/mapper/$t-decrypted" "" ""
        set +x
      fi
    done
    set -x
  fi
}

get_fsinfo()
{
  for d in "" "DISK1_" "DISK2_" "DISK3_"
  do
    if [ -z "$d" ]; then
        eval t_type="\$DISK0_TYPE"
    else
        eval t_type="\$${d}TYPE"
    fi

    if [ -n "$SFDISK" ]; then
      fs_has_partitions=1
    fi

    if [ "$t_type" = "raid" ]; then
        fs_on_raid=1
    fi

    for p in `seq 1 $MAXPART`
    do
        p="PART${p}_"

        # for raw disks without partitions, do not use PART_ in variable names
        if [ "$t_type" = "raw" ]; then
            p=""
        fi

        eval t_fs=\$${d}${p}FS
        if [ "$t_fs" = "luks" ]; then
          fs_has_luks=1
          eval luks_name=\$${d}${p}LUKSNAME
          if [ -n "$luks_name" ]; then
            eval fs=\$LUKS_${luks_name}_FS
            if [ "$fs" = "btrfs" ]; then
              fs_has_btrfs=1
            fi
          fi
        fi

        if [ "$t_fs" = "btrfs" ]; then
          fs_has_btrfs=1
        fi
     done
  done

  if [ -n "$LVM_VG" ]; then
    fs_has_lvm=1
    set +x
    for p in `seq 1 $MAXPART`
    do
      eval s=\$LVM_LV${p}_FS
      if [ "$s" = "luks" ]; then
        set -x
        fs_has_luks=1
        set +x
        break
      fi
    done
    set -x
  fi

  # legacy raid
  if [ -n "$RAID_NAME" ]; then
    fs_on_raid=1
  fi
}

mount_fs()
{
  # mount root filesystem
  if [ -z "$target" ]; then
    log_error "Error: \$target not set"
  fi
  if [ -n "$rootpart" ]; then
    part=$rootpart
    log "mount /"
    mount $part $target
    exit_on_error "Error mounting filesystem $part to $target"
  fi

  # mount other filesystems
  filesystems=""
  for d in "" "DISK1_" "DISK2_" "DISK3_"
  do
    if [ -z "$d" ]; then
        eval t_type="\$DISK0_TYPE"
    else
        eval t_type="\$${d}TYPE"
    fi

    eval t_disk=\$${d}DISK
    if [ -z "$t_disk" ]; then
      t_disk=$disk
    fi

    for p in `seq 1 $MAXPART`
    do
      part_id=$p
      p="PART${p}_"

      # for raw disks without partitions, do not use PART_ in variable names
      if [ "$t_type" = "raw" ]; then
          p=""
      fi

      eval t_mnt=\$${d}${p}MNT
      if [ -n "$t_mnt" ]; then
        if [ "$t_mnt" = "/" ]; then
          continue
        fi
        eval t_fs=\$${d}${p}FS
        if [ -z "$t_fs" ]; then
          continue
        fi
        if [ "$t_fs" = "encrypted-swap" ]; then
          continue
        fi

        if [ "$t_type" = "raw" ]; then
          part=$t_disk
          if [ ! -b "$part" ]; then
             break  # stop looping over partitions if device does not exist (eg optional RAID /dev/md0)
          fi
        else
          t_partsep=""
          if echo "$t_disk" | grep -q "[0-9]$"; then
            t_partsep="p"
          fi
          part=$t_disk$t_partsep$part_id
        fi

        filesystems="$filesystems\n$t_mnt|$part|$t_fs"

        # TODO: mount luks
      fi

      # if raw disk, stop looping over partitions
      if [ "$t_type" = "raw" ]; then
          break
      fi
    done
  done

  set +x
  for p in `seq 1 $MAXPART`
  do
    set -x
    eval subvol_path=\$BTRFS_SUBVOL${p}_PATH
    if [ -z "$subvol_path" ]; then
     continue
    fi
    subvol_internal_path=$subvol_path
    if [ "$subvol_internal_path" = "/" ]; then
      subvol_internal_path="root"
      rootpart=$btrfs_root_part
    fi
    log "btrfs create subvolume: $subvol_path"
    mkdir -p `dirname $WORK_DIR/btrfs-root/$subvol_internal_path`
    btrfs subvolume create $WORK_DIR/btrfs-root/$subvol_internal_path >&2
    exit_on_error "Error creating btrfs subvolume"

    eval subvol_limit=\$BTRFS_SUBVOL${p}_LIMIT
    if [ -n "$subvol_limit" ]; then
      btrfs quota enable $WORK_DIR/btrfs-root/$subvol_internal_path
      exit_on_error "Error enabling btrfs subvolume quota"
      if [ "$subvol_internal_path" = "/swap" ]; then
        btrfs filesystem mkswapfile --size $subvol_limit --uuid clear $WORK_DIR/btrfs-root/swap/swapfile >&2
        exit_on_error "Error creating swapfile"
      fi
      btrfs qgroup limit $subvol_limit $WORK_DIR/btrfs-root/$subvol_internal_path
      exit_on_error "Error enabling btrfs subvolume limit"
    fi

    mkdir -p $target/$subvol_path
    mount --bind $WORK_DIR/btrfs-root/$subvol_internal_path $target/$subvol_path
    exit_on_error "Error mounting btrfs subvolume"
    set +x
  done
  set -x

  if [ -n "$LVM_VG" ]; then
    for p in `seq 1 $MAXPART`
    do
      eval t_name=\$LVM_LV${p}_NAME
      if [ -z "$t_name" ]; then
        continue
      fi
      eval t_fs=\$LVM_LV${p}_FS
      eval t_mnt=\$LVM_LV${p}_MNT
      if [ -z "$t_mnt" ]; then

        if [ "$t_fs" = "encrypted-swap" ]; then
          continue
        elif [ "$t_fs" = "luks" ]; then
          eval v=\$LUKS_${t_name}_MNT
          if [ -z "$v" ]; then
            continue
          fi
          filesystems="$filesystem\n$v|/dev/mapper/$t_name-decrypted|$t_fs"
        fi
        continue
      fi

      if [ "$t_mnt" = "/" ]; then
        continue
      fi

      part=/dev/mapper/$(echo ${LVM_VG} | sed -r 's/-/--/g')-$(echo $t_name | sed -r 's/-/--/g')
      filesystems="$filesystems\n$t_mnt|$part|$t_fs"
    done
  fi

  # mount in the right order
  for fs in `/bin/echo -e "$filesystems" | sort -t"|" -k 1,1`
  do
    if [ -z "$fs" ]; then
      continue
    fi
    t_mnt=`echo $fs | cut -d"|" -f1`
    t_part=`echo $fs | cut -d"|" -f2`
    t_fs=`echo $fs | cut -d"|" -f3`
    log "mount $t_mnt ($t_fs)"
    mkdir -p $target/$t_mnt
    mount -t $t_fs $t_part $target/$t_mnt
    exit_on_error "Error mounting filesystem $t_part to $target$t_mnt"
  done
}

unmount()
{
  if mountpoint -q "$1"; then
    umount "$1"
    exit_on_error "Error umounting $1"
  fi
}

umount_fs()
{
  if [ -z "$target" ]; then
    return
  fi

  for m in `grep $target /proc/mounts | cut -d ' ' -f 2 | sort -r`; do
    umount $m
  done

  # close LUKS devices
  if [ -n "$SFDISK" ]; then
    set +x
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
        set +x
        cryptsetup luksClose /dev/mapper/$u-decrypted >&2
        set -x
      fi
    done
    set -x
  fi

  if [ -n "$LVM_VG" ]; then
    set +x
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
        set -x
        cryptsetup luksClose /dev/mapper/$t-decrypted >&2
        set +x
      fi
    done
    set -x
  fi

  if [ -n "$btrfs_root_part" ]; then
    umount $WORK_DIR/btrfs-root
  fi
}

configure_networking()
{
  # dhcp is done by network card
  # assing the IP from /proc/cmdline to the correct interface (BOOTIF)

  # copied from /usr/share/initramfs-tools/scripts/functions:

  # strip off the leading "01-", which isn't part of the mac
  # address
  temp_mac=${BOOTIF#*-}

  # convert to typical mac address format by replacing "-" with ":"
  bootif_mac=""
  IFS='-'
  for x in $temp_mac ; do
    if [ -z "$bootif_mac" ]; then
      bootif_mac="$x"
    else
      bootif_mac="$bootif_mac:$x"
    fi
  done
  unset IFS

  # look for devices with matching mac address, and set DEVICE to
  # appropriate value if match is found.
  for device in /sys/class/net/* ; do
    if [ -f "$device/address" ]; then
      current_mac=$(cat "$device/address")
      if [ "$bootif_mac" = "$current_mac" ]; then
        DEVICE=${device##*/}
        break
      fi
    fi
  done

  # end of copied

  if [ -z "$DEVICE" ]; then
    echo No network interface found for $BOOTIF
    ls /sys/class/net/ -l
    cat /sys/class/net/*/address
    busybox sh
  fi

  # FIXME: check $IP, use dhcp if empty

  CLIENT_IP=$(echo $IP | cut -d: -f1)
  ROOTSERVER=$(echo $IP | cut -d: -f2)
  GATEWAY=$(echo $IP | cut -d: -f3)
  NETMASK=$(echo $IP | cut -d: -f4)

  ip addr add $CLIENT_IP/$NETMASK dev $DEVICE
  ip link set dev $DEVICE up
  ip route add default via $GATEWAY dev $DEVICE
}

echovar()
{
    name="$1"
    value="$2"
    if [ -z "$value" ]; then
        value=`eval echo \"\\${$name}\" | sed -e 's/\`/\\\\\`/g' -e 's/\\\$/\\\\$/g'`
    fi
    echo "$name=\"$value\""
}

get_deploy_config()
{
  if echo $- | grep -q x; then
    set +x
    trace_disabled="true"
  fi
  echovar MOLIOR_TOOLS_VERSION
  echovar DEPLOYMENT_TIMESTAMP
  echovar PROJECT
  echovar VERSION
  echovar REVISION
  echovar VARIANT
  echovar SUITE
  echovar LVM_VG
  echovar ROOTDEVICE
  echovar ROOTFS
  echovar ROOTFSOPTS
  for d in "" "DISK1_" "DISK2_" "DISK3_"
  do
    if [ -z "$d" ]; then
        eval t_type="\$DISK0_TYPE"
    else
        eval t_type="\$${d}TYPE"
    fi

    eval s=\$${d}SFDISK
    test -n "$s" && echovar ${d}SFDISK "$s"
    eval s=\$${d}DISK
    test -n "$s" && echovar ${d}DISK "$s"
    eval s=\$${d}DISK_HINT
    test -n "$s" && echovar ${d}DISK_HINT "$s"

    if [ -z "$d" ]; then
        test -n "$t_type" && echovar $DISK0_TYPE "$t_type"
    else
        test -n "$t_type" && echovar ${d}TYPE "$t_type"
    fi

    if [ "$t_type" = "raid" ]; then
      eval t_raidname="\$${d}RAIDNAME"
      echovar ${d}RAIDNAME "$t_raidname"
      t_raid_id=`echo $t_raidname | tr a-z A-Z`
      eval s=\$RAID_${t_raid_id}_LEVEL
      test -n "$s" && echovar RAID_${t_raid_id}_LEVEL "$s"
      eval s=\$RAID_${t_raid_id}_DISKS
      test -n "$s" && echovar RAID_${t_raid_id}_DISKS "$s"
      eval s=\$RAID_${t_raid_id}_METADATA
      test -n "$s" && echovar RAID_${t_raid_id}_METADATA "$s"
    fi

    for p in `seq 1 $MAXPART`
    do
      p="PART${p}_"

      # for raw disks without partitions, do not use PART_ in variable names
      if [ "$t_type" = "raw" ]; then
          p=""
      fi

      eval s=\$${d}${p}FS
      test -n "$s" && echovar ${d}${p}FS "$s"
      eval s=\$${d}${p}MNT
      test -n "$s" && echovar ${d}${p}MNT "$s"
      eval s=\$${d}${p}OPTS
      test -n "$s" && echovar ${d}${p}OPTS "$s"
      eval s=\$${d}${p}MKFSOPTS
      test -n "$s" && echovar ${d}${p}MKFSOPTS "$s"

      # if raw disk, stop looping over partitions
      if [ "$t_type" = "raw" ]; then
          break
      fi
    done

    for p in `seq 1 $MAXPART`
    do
      eval s=\$${d}PART${p}_LUKSNAME
      if [ -n "$s" ]; then
        echovar ${d}PART${p}_LUKSNAME "$s"
        luks_name="$s"
        eval s=\$LUKS_${luks_name}_FS
        test -n "$s" && echovar LUKS_${luks_name}_FS "$s"
        eval s=\$LUKS_${luks_name}_MNT
        test -n "$s" && echovar LUKS_${luks_name}_MNT "$s"
        # FIXME: LUKS_${luks_name}_MKFSOPTS
      fi
    done
  done

  eval s=\$LVM_PV
  test -n "$s" && echovar LVM_PV "$s"

  for p in `seq 1 $MAXPART`
  do
    eval s=\$LVM_LV${p}_FS
    test -n "$s" && echovar LVM_LV${p}_FS "$s"
    lvm_fs="$s"
    eval s=\$LVM_LV${p}_MNT
    test -n "$s" && echovar LVM_LV${p}_MNT "$s"
    eval s=\$LVM_LV${p}_NAME
    test -n "$s" && echovar LVM_LV${p}_NAME "$s"
    eval s=\$LVM_LV${p}_OPTS
    test -n "$s" && echovar LVM_LV${p}_OPTS "$s"
    eval s="\$LVM_LV${p}_CRYPTTAB"
    test -n "$s" && echovar LVM_LV${p}_CRYPTTAB "$s"
    if [ "$lvm_fs" = "luks" ]; then
      luks_name="$s"
      eval s=\$LUKS_${luks_name}_FS
      test -n "$s" && echovar LUKS_${luks_name}_FS "$s"
      eval s=\$LUKS_${luks_name}_MNT
      test -n "$s" && echovar LUKS_${luks_name}_MNT "$s"
    fi
    eval s=\$LVM_LV${p}_SIZE
    test -n "$s" && echovar LVM_LV${p}_SIZE "$s"
    eval s=\$LVM_LV${p}_MKFSOPTS
    test -n "$s" && echovar LVM_LV${p}_MKFSOPTS "$s"

    eval s=\$BTRFS_SUBVOL${p}_PATH
    test -n "$s" && echovar BTRFS_SUBVOL${p}_PATH "$s"
    eval s=\$BTRFS_SUBVOL${p}_OPTS
    test -n "$s" && echovar BTRFS_SUBVOL${p}_OPTS "$s"
    eval s=\$BTRFS_SUBVOL${p}_LIMIT
    test -n "$s" && echovar BTRFS_SUBVOL${p}_LIMIT "$s"
  done

  echovar BIND_MOUNTS
  echovar EXTRA_BIND_MOUNTS

  if [ -n "$TARGET_HOSTNAME" ]; then
    echovar HOSTNAME $TARGET_HOSTNAME
  fi
  echovar LINUX_CMDLINE
  echovar INSTALLER_PASSWD_USERS
  echovar INSTALLER_ASK_HOSTNAME
  echovar INSTALLER_ASK_CONFIRM
  echovar INSTALLER_ASK_REBOOT
  echovar INSTALLER_USE_PXE
  echovar INSTALLER_EXTRA_INITRD
  echovar TARGET_BOOTTYPE
  echovar INSTALLER_COMPRESSION
  echovar INSTALLER_COMPRESSION_EXT
  echovar INSTALLER_ARCHIVE_TYPE
  echovar INSTALLER_ARCHIVE_EXT
  echovar INSTALLER_DISK_MIN
  echovar INSTALLER_DOWNLOAD_URL
  echovar INSTALLER_INSTALL_DISK
  echovar INSTALLER_INSTALL_DISK_HINT
  echovar INSTALLER_HOSTNAME_APPEND_MAC_IFACE
  echovar INSTALLER_EXTRA_MODULES
  echovar RAID_NAME
  echovar RAID_DEVICES
  echovar RAID_OPTS
  echovar RAID_ACCEPT_MISSING_DEVICE
  echovar ARCH
  echovar BASEVERSION
  echovar DEPLOYMENT_TYPE
  echovar DEPLOYMENT $deployment
  echovar DEPLOYMENT_OUTPUT_FILE
  echovar INSTALLER_EJECT_DISK
  for var in `set | awk -F= '/^EXTRA_.*/ {print $1}'`; do
    echovar $var
  done
  if [ -n "$DEPLOYMENT_ENV_EXTRA" ]; then
    for var in $DEPLOYMENT_ENV_EXTRA; do
      echovar $var
    done
  fi
  if [ "$trace_disabled" = "true" ]; then
    set -x
  fi
}

mount_fs_devices()
{
  if [ -z "$target" ]; then
    log_error "Error: \$target not set"
  fi

  # on Debian/buster /run/udev seems to be needed for installing grub
  if [ -d "/run/udev/" ]; then
    mkdir -p $target/run/udev
    mount --bind /run/udev/ $target/run/udev
  fi
}

umount_fs_devices()
{
  if [ -z "$target" ]; then
    return
  fi
  if [ -d "/run/udev" ]; then
    umount $target/run/udev/
    rmdir $target/run/udev/
  fi
}

install_grub_bootloader()
{
  if [ -z "$target" ]; then
    log_error "Error: \$target not set"
  fi

  if [ -z "$rootpart" ]; then
    log_error "Error updating grub: no rootpart defined"
  fi

  mount_fs_devices

  uuid=`blkid  -o value -s UUID $rootpart`
  t="bios"
  if [ -n "$TARGET_BOOTTYPE" ]; then
      t=$TARGET_BOOTTYPE
  fi
  log "grub-install ($t)"
  if [ "$TARGET_BOOTTYPE" = "efi" ]; then
    if [ "$ARCH" = "amd64" ]; then
      GRUBEFITYPE=x86_64-efi
    elif [ "$ARCH" = "arm64" ]; then
      GRUBEFITYPE=arm64-efi
    else
      log_error "Unsupported EFI Architecture: $ARCH"
    fi
    chroot $target grub-install --removable --recheck --force --no-floppy --target $GRUBEFITYPE >&2
    exit_on_error "installing grub failed"
  else
    chroot $target grub-install --modules part_msdos --recheck --force --no-floppy $disk >&2
    exit_on_error "installing grub failed"
    # configure root device
    chroot $target debconf-set-selections <<EOFGRUB
grub-pc grub-pc/install_devices multiselect $disk
EOFGRUB
  fi

  log "update-grub"
  if [ -n "$LINUX_CMDLINE" ]; then
    sed -i "s%^GRUB_CMDLINE_LINUX=.*$%GRUB_CMDLINE_LINUX=\"$LINUX_CMDLINE\"%" $target/etc/default/grub
  fi
  mkdir -p $target/dev/disk/by-uuid
  ln -sf $rootpart $target/dev/disk/by-uuid/$uuid
  chroot $target update-grub

  rm -rf $target/dev/disk/by-uuid
  umount_fs_devices
}

auto_select_disk()
{
  disk=""
  eval disk="\$$1"
  selection_policy="$2"
  disk_hint=""
  eval disk_hint="\$${1}_HINT"
  # NOTE: after we get arguments 1 and 2 we shift twice to get all the others in selectable_disks
  shift 2
  selectable_disks="$@"
  selected_disk=""

  if [ -n "$selectable_disks" ]; then
    if [ "$selection_policy" = "hint" ]; then
      if [ "$disk_hint" = "largest" ]; then
        selected_disk=$(lsblk -dn -x SIZE -o PATH $(echo "$selectable_disks" | tr "\n" " ") | tail -n1)
      elif [ "$disk_hint" = "smallest" ]; then
        selected_disk=$(lsblk -dn -x SIZE -o PATH $(echo "$selectable_disks" | tr "\n" " ") | head -n1)
      fi
    # Pick any physical disk for disks without specified hints.
    elif [ "$selection_policy" = "any" ]; then
      selected_disk=$(echo "$selectable_disks" | tail -n1)
    fi
  fi

  echo "$selected_disk"
}
