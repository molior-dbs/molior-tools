#!/bin/sh
# vim: syntax=sh

extract_layers()
{
    tar -mp --delay-directory-restore -xf $1
    dirs=`find -maxdepth 1 -type d`
    for dir in $dirs
    do
        if [ "$dir" = "." ]; then
            continue
        fi
        mkdir $dir/layer
        cd $dir/layer
        tar -mp --delay-directory-restore -xf ../layer.tar
        exit_on_error "Error extracting docker image layer"
        rm -f ../layer.tar
        cd - > /dev/null
    done
}

assemble_layers()
{
    dirs=`find -maxdepth 1 -type d`
    for dir in $dirs
    do
        if [ "$dir" = "." ]; then
            continue
        fi
        cd $dir/layer
        tar cf ../layer.tar .
        exit_on_error "Error assembling docker image layer"
        cd - > /dev/null
        rm -rf $dir/layer
    done
}

docker_deduplicate()
{
    old_tar=$1
    new_tar=$2
    out_tar=$3

    tmpdir=`mktemp -d $WORK_DIR/dedupwork_XXXXXX`
    mkdir $tmpdir/dedupwork
    cd $tmpdir
        cd dedupwork
            mkdir old
            cd old
                extract_layers $old_tar
            cd ..

            mkdir new
            cd new
                extract_layers $new_tar
            cd ..
        cd ..

        # remove duplicates from old, so we do not include them in redup
        rdfind -makeresultsfile false -removeidentinode false -deleteduplicates true dedupwork/old >/dev/null
        exit_on_error "Error finding duplicate files in docker image"

        # remove duplicates
        rdfind -outputname deduplicated.files -removeidentinode false -deleteduplicates true dedupwork/old dedupwork/new >/dev/null
        exit_on_error "Error finding duplicate files in docker image"
        grep -v '^#' deduplicated.files | sed -e 's/DUPTYPE_FIRST_OCCURRENCE[0-9 -]*/I/' \
                                              -e 's/DUPTYPE_OUTSIDE_TREE[0-9 -]*/O/' \
                                              -e 's/DUPTYPE_WITHIN_SAME_TREE[0-9 -]*/O/' > dedupwork/new/deduplicated.files
        rm -f deduplicated.files

        cd dedupwork
            cd new
                assemble_layers
                tar cf $out_tar .
                exit_on_error "Error creating deduplicated docker image"
            cd ..
        cd ..
    cd ..
    rm -rf $tmpdir
}

docker_reduplicate()
{
    old_tar=$1
    dedup_tar=$2
    out_tar=$3

    if [ -z "$out_tar" ]; then
        out_tar=../../docker-containers.redup.tar
    fi

    tmpdir=`mktemp -d dedupwork_XXXXXX`
    mkdir $tmpdir/dedupwork
    cd $tmpdir
        cd dedupwork
            mkdir old
            cd old
                extract_layers $old_tar
            cd ..

            mkdir new
            cd new
                extract_layers $dedup_tar
                mv deduplicated.files ../
            cd ..
        cd ..

        dedups=`cat dedupwork/deduplicated.files`
        total=`wc -l dedupwork/deduplicated.files`
        old_IFS="$IFS"
        IFS="
" # IFS is newline

        input=""
        output=""
        for entry in $dedups
        do
            case $entry in
                I*)
                    input="${entry#I}"
                    ;;
                O*)
                    ln $input "${entry#O}"
                    ;;
            esac
        done

        IFS=$old_IFS

        cd dedupwork
            cd new
                assemble_layers
                tar cf ../../../docker-containers.redup.tar .
            cd ..
        cd ..
    cd ..
    rm -rf $tmpdir
}
