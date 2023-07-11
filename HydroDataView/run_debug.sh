#!/bin/bash

set -e

. .venv/bin/activate
flask --app hydro_data_view.app:app run --debug
