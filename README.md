# molior-tools

This repository provides the following molior-tools:

- add-molior-repo
- create-release
- molior-create
- molior-deploy
- molior-parseconfig
- molior-project-manager
- molior-sbuild
- molior-trigger

## add-molior-repo

Adds the corresponding `source.list` entry of a molior project to your `/etc/apt/source.list.d` directory.

## create-release

Release a debian package including modifying the debian/changelog.
This is done by creating an anotated tag in the git
repository and adding to the debian changelog.

## molior-create

## molior-deploy

Create deployments of a project

## molior-parseconfig

Simple tool to read/edit the molior.yml config file.

## molior-project-manager

Do an operation (for example moving to another project version)
for multiple source repositories.

## molior-sbuild

Reproduce molior builds in the same environment as the molior server localy.
The correct environment will be fetched from the `molior.yml` file.

## molior-trigger

Trigger a build on the molior server for a git repo
