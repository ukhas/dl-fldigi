#!/usr/bin/env python

# Copyright 2012 Daniel Richman
# Copyright 2015 Adam Greig
# DL-Fldigi update check server

import os
import yaml
import os.path
import subprocess
from flask import Flask, abort, request, jsonify

app = Flask(__name__)

# Load config from file
app_dir = os.path.abspath(os.path.dirname(__file__))
config_file = os.path.join(app_dir, "config.yml")
config = yaml.load(open(config_file))

# Swap to directory containing this script, to ensure we're inside
# the git repository.
os.chdir(app_dir)


def git_rev_list(commit):
    commits = subprocess.check_output(["git", "rev-list", commit + "^{}"])
    return set(commits.split("\n")[1:])

# Store commits considered old
old_commits = git_rev_list(config['latest_release'])


@app.route("/")
def check():
    try:
        commit = request.args["commit"]

        if commit in old_commits:
            return jsonify(config["update"])
        else:
            return ""

    except KeyError:
        # bad platform or missing arg
        abort(400)

if __name__ == "__main__":
    app.run(debug=True)
