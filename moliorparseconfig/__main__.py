"""
Provides the Molior Parse Config Command Line Tool
"""
import sys
from pathlib import Path

import yaml
import click

CONFIG_PATH = Path("debian/molior.yml")


class MoliorConfig:
    """
    Represents the molior.yml file.
    """

    def __init__(self, path=CONFIG_PATH):
        self.__path = path
        self.__cfg = None

    def write(self):
        """
        Dumps config to file.
        """
        with self.__path.open("w") as cfg_file:
            yaml_data = yaml.dump(self.__cfg, default_flow_style=False)
            cfg_file.write(yaml_data)
        self.load()

    def load(self):
        """
        Loads the molior.yml file.
        """
        with self.__path.open() as cfg_file:
            self.__cfg = yaml.safe_load(cfg_file)

    def __getattr__(self, name):
        if not self.__cfg:
            self.load()

        return self.__cfg.get(name)

    @property
    def targets(self):
        """Sets targets"""
        if not self.__cfg:
            self.load()

        return self.__cfg.get("targets")

    @targets.setter
    def targets(self, targets):
        self.__cfg["targets"] = targets
        self.write()

    @property
    def config_version(self):
        """Set config version"""
        if not self.__cfg:
            self.load()

        return self.__cfg.get("config_version")

    @config_version.setter
    def config_version(self, config_version):
        self.__cfg["config_version"] = config_version
        self.write()

    def upgrade(self, project, version):
        """
        Upgrades the config to the latest version.
        """
        targets = {project: [version]}
        self.__cfg.pop("target_repo_version")
        self.targets = targets
        self.config_version = "1"


@click.group()
def cli():
    """
    Molior Parse Config Command Line Tool
    """
    pass


@click.command(name="version")
def cli_version():
    """
    Returns the configuration version.
    """
    config_version = MoliorConfig().config_version
    if not config_version:
        click.echo("No config version found")
        sys.exit(1)

    click.echo(config_version)


@click.group(name="target")
def cli_target():
    """
    Parses the targets configuration.
    """
    pass


@click.command(name="show")
@click.argument("target_name")
def target_show(target_name):
    """
    Returns the configured versions of the given target project.
    """
    cfg = MoliorConfig()
    targets = cfg.targets

    if not targets:
        click.echo("No configured targets")
        sys.exit(1)

    versions = targets.get(target_name)
    if not versions:
        click.echo("Target '{}' not found".format(target_name))
        sys.exit(1)

    for ver in versions:
        click.echo(ver)


@click.command(name="list")
@click.option("--quiet", is_flag=True)
def target_list(quiet):
    """
    Returns a list of all configured targets.
    """
    cfg = MoliorConfig()
    targets = cfg.targets

    if not targets:
        not quiet and click.echo("No configured targets")  # pylint: disable=expression-not-assigned
        sys.exit(1)

    for target_name in targets.keys():
        for version in targets.get(target_name):
            click.echo("{}/{}".format(target_name, version))


@click.command(name="add")
@click.argument("project")
@click.argument("projectversion")
def target_add(project, projectversion):
    """
    Adds a new target to the configuration.
    """
    cfg = MoliorConfig()
    targets = cfg.targets

    if project in targets and projectversion in targets.get(project):
        click.echo("Target '{}/{}' already defined".format(project, projectversion))
        sys.exit(1)

    if not targets.get(project):
        targets[project] = []

    targets[project].append(projectversion)
    cfg.targets = targets
    click.echo("Target '{}/{}' added".format(project, projectversion))


@click.command(name="remove")
@click.argument("project")
@click.argument("projectversion")
def target_remove(project, projectversion):
    """
    Removes a target from the configuration.
    """
    cfg = MoliorConfig()
    targets = cfg.targets

    if project not in targets or projectversion not in targets.get(project):
        click.echo("Target '{}/{}' not found".format(project, projectversion))
        sys.exit(1)

    targets[project].remove(projectversion)
    if not targets.get(project):
        targets.pop(project)

    cfg.targets = targets
    click.echo("Target '{}/{}' removed".format(project, projectversion))


@click.command(name="update")
@click.argument("project")
@click.argument("old_projectversion")
@click.argument("new_projectversion")
def target_update(project, old_projectversion, new_projectversion):
    """
    Updates a target from the configuration.
    """
    cfg = MoliorConfig()
    targets = cfg.targets

    if project not in targets or old_projectversion not in targets.get(project):
        click.echo("Target '{}/{}' not found".format(project, old_projectversion))
        sys.exit(1)

    targets[project].remove(old_projectversion)
    targets[project].append(new_projectversion)

    cfg.targets = targets
    click.echo("Target '{}/{}' updated".format(project, new_projectversion))


@click.command(name="target_repo_version")
def cli_target_repo_version():
    """
    Returns the target repo version from old config.
    """
    cfg = MoliorConfig()
    target = cfg.target_repo_version
    if not target:
        click.echo("Target Repo Version not found")
        sys.exit(1)

    click.echo(target)


@click.command(name="upgrade")
@click.argument("project")
def cli_upgrade(project):
    """
    Upgrade config version to the latest one.
    """
    cfg = MoliorConfig()
    if cfg.config_version:
        click.echo("Config already in latest format")
        sys.exit(1)

    target = cfg.target_repo_version
    if not target:
        click.echo("Target Repo Version not found")
        sys.exit(1)

    cfg.upgrade(project, target)
    click.echo("Configuration upgraded")


def main():
    """
    Starts the command line interface.
    """
    if not Path(CONFIG_PATH).exists():
        click.echo("debian/molior.yml not found")
        sys.exit(1)

    # target sub commands
    cli_target.add_command(target_show)
    cli_target.add_command(target_list)
    cli_target.add_command(target_add)
    cli_target.add_command(target_remove)
    cli_target.add_command(target_update)

    # cli sub commands
    cli.add_command(cli_target)
    cli.add_command(cli_version)
    cli.add_command(cli_target_repo_version)
    cli.add_command(cli_upgrade)

    cli()


if __name__ == "__main__":
    main()
