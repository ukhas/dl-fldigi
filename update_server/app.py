#!/usr/bin/env python

# Copyright 2012 Daniel Richman
# DL-Fldigi update check server

import os.path
import yaml
from flask import Flask, abort, request, jsonify

app_dir = os.path.dirname(__file__)
config_file = os.path.join(app_dir, "config.yml")
config = {}

app = Flask(__name__)

def load_config():
    global config

    mtime = os.stat(config_file).st_mtime
    if mtime != config.get("_mtime", None):
        with open(config_file) as f:
            config = yaml.load(f)
        config["_mtime"] = mtime

@app.route("/")
def check():
    load_config()

    try:
        platform = request.args["platform"]
        commit = request.args["commit"]
        expect = config["expect"][platform]

        if isinstance(expect, list) and commit in expect:
            return ""
        elif isinstance(expect, basestring) and expect == commit:
            return ""
        else:
            return jsonify(config["update"])

    except KeyError:
        # bad platform or missing arg
        abort(400)

if __name__ == "__main__":
    app.run()
