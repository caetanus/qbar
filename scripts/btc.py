#!/usr/bin/env python3


import configparser
import time
import json
import os
import sys
import requests
from decimal import Decimal
from os.path import expanduser

from signal import signal, SIGALRM, alarm

def timeout(*args):
    print("\nexiting by timeout")
    sys.exit(1)
    os.kill(os.getpid(), 9)

signal(SIGALRM, timeout)
alarm(20)

waybar = False
if "--waybar" in sys.argv:
    waybar = True
icon =''
icon = ''
url = 'https://api.binance.com/api/v3/ticker/24hr?symbol=BTCUSDT'
out = requests.get(url).json()
local_price = round(Decimal(out['lastPrice']), 2)
local_price_m = '{:.2f}'.format(local_price/1000)
chang_fl = float(out['priceChangePercent'])
change_24 = "{:.2f}".format(chang_fl)
var = '▲' if chang_fl > 0 else '▼'

icon_waybar = 'up' if chang_fl > 0 else 'down'
color = '#339944' if chang_fl > 0 else '#993344'

polycolor = '%{F#339944}' if chang_fl > 0 else '%{F#993344}'
var_poly = f'{polycolor}{var}'"%{F-}"

if waybar:
    sys.stdout.write(json.dumps({"text": f'<span color="{color}">{icon}</span> {var} {local_price_m}',
                  "icon": icon_waybar,
                  "class": color,
                  "tooltip":"",
                  "alt": "",
                  "percentage": float(change_24)}))
    sys.exit(0)

display_opt = 'both'# config['general']['display']
if display_opt == 'both' or display_opt is None:
    sys.stdout.write(f'{icon} {var_poly} {local_price_m}({change_24}%)')
elif display_opt == 'percentage':
    sys.stdout.write(f'{icon} {var_poly} {change_24}%')
elif display_opt == 'price':
    sys.stdout.write(f'{icon} {local_price_m}{var_poly}')
