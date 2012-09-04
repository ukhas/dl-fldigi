#!/usr/bin/env python

# Copyright 2012 Daniel Richman
# DL-Fldigi update check server

import os.path
import json
import yaml
from flask import Flask, abort, request

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

        if expect == commit:
            return ""
        else:
            return json.dumps(config["update"])

    except KeyError:
        # bad platform or missing arg
        abort(400)

if __name__ == "__main__":
    app.run()
