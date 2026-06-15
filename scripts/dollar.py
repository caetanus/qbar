#! /bin/env python3
import sys
import time
import requests
import json
class NetworkError(IOError):
    pass


url = "https://br.dolarapi.com/v1/cotacoes/usd"


def dollar(output='polybar'):
    polybar = output == "polybar"

    #try:

    #response = requests.get("https://economia.awesomeapi.com.br/last/USD-BRL").json()['USDBRL']
    response = requests.get(url).json()

    change = response['compra'] - response['fechoAnterior']

    value = "{:.2f}".format(float(response['compra']))
    icon = '▲' if float(change) > 0 else '▼'
    icon_waybar = 'up' if float(change) > 0 else 'down'

    color = '#339944' if float(change) > 0 else '#1d2020'
    var = '%%F{%s}%s''%%{F-}' % (color, icon)
    change = "{:.2f}".format(float(change))
    if polybar:
        print(f" {var} {value}({change}%)")
    else:
        sys.stdout.write(json.dumps({"text": f'<span color="{color}">{icon}</span>  {value}',
                          "icon": icon_waybar,
                          "class": color,
                          "tooltip":"",
                          "alt": "",
                          "percentage": float(change)}))
        sys.stdout.flush()

    #except:
    #    raise NetworkError

arg = 'json' if '--waybar' in sys.argv else 'polybar'
failure = False
for i in range(50):
    
    try:
        dollar('json' if '--waybar' in sys.argv else  "polybar")
        failure = False
        break
    except NetworkError:
        failure = True
        time.sleep(0.5)
        pass

if failure and arg == 'polybar':
    sys.stdout.write("%{F#993344}USD not available%{F-}")
    sys.stdout.flush()
