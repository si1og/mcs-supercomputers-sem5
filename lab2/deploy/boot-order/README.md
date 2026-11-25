# Порядок запуска GlusterFS

Настройки для `server01`, `server02`, `server03` на openSUSE Leap 16.0 с NetworkManager.

После перезагрузки `server03` исходящие соединения glusterd остались в `SYN-SENT` с исходным NAT-адресом `10.0.2.15`, хотя позже маршрут через `enp0s9` уже стал правильным. В штатном unit было `After=network-online.target` без зависимости, запускающей сам target. Поэтому NetworkManager-wait-online при загрузке не выполнялся.

## Что изменено

- Drop-in `/etc/systemd/system/glusterd.service.d/60-boot-order.conf` запрашивает `network-online.target` и требует успешного завершения `NetworkManager-wait-online.service`.
- `RequiresMountsFor=/bricks/rep` добавляет зависимость и порядок относительно XFS, на которой лежит brick. При ошибке монтирования glusterd не должен стартовать с этим brick на корневой ФС.
- `ExecStartPre=/usr/local/libexec/gluster-wait-network` проверяет локальный адрес `enp0s9` и маршруты к остальным двум узлам. Максимальное ожидание — 90 секунд; при неправильном исходном адресе запуск glusterd завершается ошибкой.
- У активного серверного профиля `gluster-network` задано `ipv4.may-fail=no`, автоподключение включено.
- На server03 отключено автоподключение неактивного дублирующего профиля `8e657444-284b-45d9-88f6-176cbab5a709` с адресом клиента `192.168.100.14/24`. Профиль сохранён, действующий серверный профиль `.13` не переподключался.

Порядок:

```text
NetworkManager → завершение настройки сети → проверка внутреннего IPv4/маршрутов ┐
LVM → XFS /bricks/rep                                                         ├→ glusterd → brick и self-heal
gluster-loopback.socket                                                      ┘
```

Остальные серверы могут быть выключены: скрипт не использует ping и не ждёт чужих glusterd. Проверяется только локальная готовность. При потере сети после запуска это условие не является постоянным мониторингом.

## Установка

Скопировать содержимое этой директории на соответствующий сервер и выполнить:

```bash
sudo bash ./install.sh
```

Установщик проверяет UUID действующего профиля, применяет параметры без отключения интерфейса и перезапускает glusterd. Отличающиеся существующие файлы не перезаписываются. Проверены установка и перезапуск glusterd; полная перезагрузка машин при установке не выполняется.

## Проверка

```bash
systemctl cat glusterd
systemctl status network-online.target NetworkManager-wait-online.service bricks-rep.mount glusterd
sudo journalctl -u glusterd -b --no-pager
ip route get 192.168.100.11
sudo gluster peer status
sudo gluster volume status repvol
```

В журнале glusterd перед его стартом должна появиться строка `Gluster network ready: enp0s9 source 192.168.100.XX`.

Для будущих томов на отдельных файловых системах добавить их точки монтирования к `RequiresMountsFor`, например:

```ini
[Unit]
RequiresMountsFor=/bricks/rep /bricks/dist /bricks/arb
```

Добавлять пути следует после создания соответствующих записей в `/etc/fstab`, затем выполнять `sudo systemctl daemon-reload`.

## Откат

Удалить только `/etc/systemd/system/glusterd.service.d/60-boot-order.conf` и `/usr/local/libexec/gluster-wait-network`, затем выполнить `sudo systemctl daemon-reload`. При необходимости вернуть `ipv4.may-fail=yes` нужному профилю по UUID. Лишний профиль клиента на server03 лучше оставить без автоподключения.
