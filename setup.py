from setuptools import setup, find_packages

setup(
    name="molior-tools",
    install_requires=[
        "click",  # used by molior-parseconfig
        "requests"
    ],
    packages=find_packages(),
    py_modules=["moliorctl"],
    entry_points={
        "console_scripts": [
            "molior-parseconfig = moliorparseconfig.__main__:main",
            "molior-ctl = moliorctl:main",
        ]
    }
)
