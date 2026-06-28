#!/bin/bash

killall -q polybar

polybar bottom &
polybar top &

