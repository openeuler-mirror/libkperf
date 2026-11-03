#!/usr/bin/env python3
# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# libkperf licensed under the Mulan PSL v2.

"""Remove files left by an earlier libkperf Python installation."""

from __future__ import print_function

import glob
import os
import shutil
import site
import sysconfig


PACKAGE_NAMES = ("_libkperf", "kperf", "ksym")
METADATA_PATTERNS = (
    "libkperf.egg-info",
    "libkperf-*.egg-info",
    "libkperf.dist-info",
    "libkperf-*.dist-info",
    "libkperf-*.egg",
)


def site_directories():
    """Return the install directories used by the selected interpreter."""
    candidates = []

    try:
        candidates.extend(site.getsitepackages())
    except AttributeError:
        pass

    user_site = site.getusersitepackages()
    if isinstance(user_site, str):
        candidates.append(user_site)
    else:
        candidates.extend(user_site)

    paths = sysconfig.get_paths()
    candidates.extend((paths.get("purelib"), paths.get("platlib")))

    directories = []
    seen = set()
    for path in candidates:
        if not path:
            continue
        path = os.path.abspath(path)
        key = os.path.normcase(path)
        if key not in seen:
            seen.add(key)
            directories.append(path)
    return directories


def remove_path(path):
    if os.path.islink(path) or os.path.isfile(path):
        os.remove(path)
    elif os.path.isdir(path):
        shutil.rmtree(path)
    else:
        return False

    print("removed {}".format(path))
    return True


def clean_easy_install_pth(directory):
    """Remove references to a deleted libkperf egg."""
    pth_path = os.path.join(directory, "easy-install.pth")
    if not os.path.isfile(pth_path):
        return False

    with open(pth_path, "r") as pth_file:
        lines = pth_file.readlines()
    remaining = [line for line in lines if "libkperf-" not in line.lower()]
    if remaining == lines:
        return False

    temporary_path = pth_path + ".libkperf.tmp"
    with open(temporary_path, "w") as temporary_file:
        temporary_file.writelines(remaining)
    os.replace(temporary_path, pth_path)
    print("updated {}".format(pth_path))
    return True


def main():
    removed = False
    for directory in site_directories():
        for package_name in PACKAGE_NAMES:
            removed = remove_path(os.path.join(directory, package_name)) or removed

        for pattern in METADATA_PATTERNS:
            for path in glob.glob(os.path.join(directory, pattern)):
                removed = remove_path(path) or removed

        removed = clean_easy_install_pth(directory) or removed

    if not removed:
        print("no previous libkperf Python installation found")


if __name__ == "__main__":
    main()
