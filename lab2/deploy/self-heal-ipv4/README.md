# Постоянное исправление запуска self-heal в openSUSE

Установлено на `server01`, `server02`, `server03` 23 ноября 2026 года.

В пакете GlusterFS 11.1 процесс self-heal при начальном подключении к `localhost` выбирает `::1`, а настроенный на IPv4 `glusterd` слушает `0.0.0.0:24007`. Из-за этого self-heal завершался с `Exhausted all volfile servers`, хотя настройка `cluster.self-heal-daemon` была включена.

Решение: локальная пересылка `[::1]:24007` в `127.0.0.1:24007` через установленный `systemd-socket-proxyd`. Снаружи IPv6-сокет недоступен. Межсерверная сеть и brick-процессы продолжают использовать IPv4. Это обход проблемы пакета, а не изменение исходного кода GlusterFS.

## Установленные файлы

| Исходник | Назначение на сервере |
|---|---|
| `gluster-loopback.socket` | `/etc/systemd/system/gluster-loopback.socket` |
| `gluster-loopback.service` | `/etc/systemd/system/gluster-loopback.service` |
| `50-loopback.conf` | `/etc/systemd/system/glusterd.service.d/50-loopback.conf` |
| `gluster_loopback.te` | Локальный SELinux-модуль `gluster_loopback` |

Socket включён в автозагрузку и запускается до `glusterd`. Proxy-служба активируется при первом соединении self-heal. Ручной запуск self-heal и настройки для каждого нового тома не требуются.

SELinux остаётся в режиме `Enforcing`. Локальный модуль разрешает домену `systemd_socket_proxyd_t` привязку и подключение к `gluster_port_t`, чтение атрибутов namespace и отправку уведомления готовности PID 1. Разрешения добавлены по фактическим AVC-запретам openSUSE. Первоначальный запуск без этого модуля завершался ошибкой `Permission denied`.

## Повторная установка

Скопировать все файлы этой директории на сервер, затем из полученной директории выполнить:

```bash
sudo bash ./install.sh
```

Требуются установленные `systemd-socket-proxyd`, `checkmodule`, `semodule_package`, `semodule`. Установщик не перезаписывает отличающиеся существующие unit-файлы. Он устанавливает SELinux-модуль, включает socket и перезапускает proxy-службу, если она уже работала, и `glusterd`. Это может вызвать краткое переподключение клиентов.

## Проверка

```bash
systemctl is-enabled gluster-loopback.socket
systemctl is-active gluster-loopback.socket gluster-loopback.service glusterd
sudo ss -ltnp | grep 24007
sudo gluster volume status repvol
sudo gluster volume heal repvol info
/usr/sbin/getenforce
```

Ожидается `Online Y` у всех self-heal daemon, `Connected` у brick и `Enforcing` у SELinux. Proxy-служба может оставаться неактивной до первого подключения, если запущенных replica/dispersed-томов нет.

При установке проверены успешный перезапуск `glusterd`, автоматический запуск self-heal на всех узлах и пустая очередь heal. Полная перезагрузка виртуальных машин не выполнялась.

## Диагностика

```bash
sudo journalctl -u gluster-loopback.socket -u gluster-loopback.service -n 50 --no-pager
sudo tail -n 50 /var/log/glusterfs/glustershd.log
sudo ausearch -m AVC,USER_AVC -ts recent
```

## Откат

Отключить socket и proxy-службу, затем удалить только установленные здесь три файла и локальный SELinux-модуль:

```bash
sudo systemctl disable --now gluster-loopback.socket
sudo systemctl stop gluster-loopback.service
sudo rm -i /etc/systemd/system/gluster-loopback.socket \
  /etc/systemd/system/gluster-loopback.service \
  /etc/systemd/system/glusterd.service.d/50-loopback.conf
sudo semodule -r gluster_loopback
sudo systemctl daemon-reload
```

После отката исходная проблема запуска self-heal вернётся, если сам пакет GlusterFS ещё не исправлен.
