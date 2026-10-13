# Развёртывание мастер-сервера

Мастер-сервер — это лобби (комнаты по коду, обмен ICE-кандидатами) плюс STUN/TURN для пробития NAT и ретрансляции.

## Порты

| Порт | Протокол | Назначение |
|---|---|---|
| 27400 | TCP | лобби |
| 3478 | UDP | STUN/TURN |
| 49152–49352 | UDP | TURN relay |

## Сборка (Linux)

```
sudo apt install build-essential cmake ninja-build git
cmake --preset linux-server
cmake --build --preset linux-server --target vw_master
```

Бинарник: `build/linux-server/bin/vw_master`.

## Установка

```
sudo useradd --system --no-create-home voidwarzone
sudo mkdir -p /opt/voidwarzone /etc/voidwarzone
sudo cp build/linux-server/bin/vw_master /opt/voidwarzone/
sudo cp deploy/vw-master.service /etc/systemd/system/
```

`/etc/voidwarzone/master.env` (права 600):

```
EXTERNAL_IP=<публичный IP сервера>
TURN_USER=void
VW_TURN_PASS=<длинный случайный пароль>
```

```
sudo ufw allow 27400/tcp
sudo ufw allow 3478/udp
sudo ufw allow 49152:49352/udp
sudo systemctl daemon-reload
sudo systemctl enable --now vw-master
journalctl -u vw-master -f
```

## Клиенты

Адрес мастера: `vw_client --master host[:port]` или переменная окружения `VW_MASTER`. STUN/TURN-параметры клиент получает от мастера при подключении.
