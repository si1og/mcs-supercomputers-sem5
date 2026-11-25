# Автоматическое монтирование repvol на client

Установлено 23 ноября 2026 года. Файл `repvol.fstab` содержит запись, добавленную в `/etc/fstab` клиента. Исходный fstab сохранён в `/etc/fstab.before-gluster-client-20261123`.

- `_netdev` задаёт сетевое монтирование и ожидание `network-online.target`.
- `xlator-option=transport.address-family=inet` задаёт IPv4 при первоначальном запросе конфигурации.
- `backup-volfile-servers=server02:server03` задаёт резервные серверы конфигурации.
- `nofail` позволяет загрузить клиент, если кластер недоступен.
- `x-systemd.mount-timeout=90s` ограничивает ожидание монтирования.

Включён `NetworkManager-wait-online.service`. У действующего профиля внутренней сети `41db1fb6-476e-47ea-8b14-2bbde55458c9` установлено `ipv4.may-fail=no`. Остальные сетевые профили не изменялись.

Это монтирование при загрузке. Если кластер был недоступен и попытка завершилась тайм-аутом, после его возвращения повторить:

```bash
sudo systemctl start mnt-repvol.mount
```

## Установка

Скопировать содержимое директории на client и выполнить:

```bash
sudo bash ./install.sh
```

Установщик не добавляет повторную запись и отказывается заменять отличающуюся запись того же тома. Проверка выполняет временное монтирование с новыми параметрами, читает сведения о ФС и размонтирует его. Действующий `/mnt/repvol` не размонтируется.

## Проверка

```bash
findmnt /mnt/repvol
df -hT /mnt/repvol
systemctl cat mnt-repvol.mount
systemctl show mnt-repvol.mount -p FragmentPath -p Wants -p After
grep repvol /etc/fstab
```

Ожидаемый источник — `server01:/repvol`, тип — `fuse.glusterfs`. Unit генерируется из fstab в `/run/systemd/generator/mnt-repvol.mount` и ожидает сеть.

При установке проверены синтаксис fstab, сгенерированный unit и успешное новое монтирование тома. Полная перезагрузка клиента не выполнялась.

Пакетный `mount.glusterfs` выводит предупреждения про `nofail` и `grep: warning: stray \\ before -`; при проверке они не помешали успешному монтированию. `nofail` обрабатывается systemd при генерации unit.
