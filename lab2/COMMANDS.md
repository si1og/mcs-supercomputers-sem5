# GlusterFS: справочник команд

Краткая шпаргалка для стенда:

```text
server01  192.168.100.11
server02  192.168.100.12
server03  192.168.100.13
client    192.168.100.14
```

## Проверка системы

```bash
hostname
ip -br addr
getent hosts server01 server02 server03 client
lsblk -f
rpm -qa | grep -i gluster
systemctl status glusterd
```

## LVM thin

Следующие команды уничтожают прежние сигнатуры на выбранном диске. Перед выполнением проверить `/dev/sdb` через `lsblk -f` и `wipefs -n`.

```bash
sudo pvcreate /dev/sdb
sudo vgcreate vg_gluster /dev/sdb
sudo lvcreate -L 18G --thinpool thinpool vg_gluster
sudo lvcreate -V 4G -T vg_gluster/thinpool -n lv_rep
```

Мониторинг:

```bash
sudo pvs
sudo vgs
sudo lvs -a -o lv_name,lv_size,pool_lv,data_percent,metadata_percent,chunksize
```

## XFS и brick

Соглашение об именовании каталогов brick:

```text
repvol  → /bricks/rep/repvol
distvol → /bricks/dist/distvol
arbvol  → /bricks/arb/arbvol
```

На каждом сервере:

```bash
sudo mkfs.xfs -f -i size=512 /dev/vg_gluster/lv_rep
sudo mkdir -p /bricks/rep
sudo mount /dev/vg_gluster/lv_rep /bricks/rep
sudo mkdir -p /bricks/rep/repvol
sudo blkid /dev/vg_gluster/lv_rep
```

Строка `/etc/fstab`, UUID на каждом сервере свой:

```fstab
UUID=<UUID> /bricks/rep xfs defaults,noatime 0 2
```

Проверка:

```bash
sudo mount -a
findmnt /bricks/rep
df -hT /bricks/rep
ls -ld /bricks/rep/repvol
```

## Настройка glusterd

Зависимости запуска от сети и XFS: [deploy/boot-order](deploy/boot-order/README.md).

В `/etc/glusterfs/glusterd.vol` на всех серверах:

```text
volume management
    ...
    option transport.address-family inet
    option base-port 49152
    option max-port 49251
end-volume
```

Применение:

```bash
sudo systemctl restart glusterd
grep -nE 'transport.address-family|base-port|max-port' /etc/glusterfs/glusterd.vol
```

## Firewall

На всех серверах:

```bash
sudo firewall-cmd --permanent --add-port=24007-24008/tcp
sudo firewall-cmd --permanent --add-port=49152-49251/tcp
sudo firewall-cmd --reload
sudo firewall-cmd --list-ports
```

## Trusted storage pool

На `server01`:

```bash
sudo gluster peer probe server02
sudo gluster peer probe server03
sudo gluster peer status
sudo gluster pool list
```

Проверить согласованность с каждого сервера:

```bash
ssh server01 sudo gluster pool list
ssh server02 sudo gluster pool list
ssh server03 sudo gluster pool list
```

Удалить peer:

```bash
sudo gluster peer detach <hostname>
sudo gluster peer detach <hostname> force
```

Диагностика:

```bash
sudo tail -n 100 /var/log/glusterfs/glusterd.log
ss -ltn | grep 24007
```

## Replica-том

Создать и запустить:

```bash
sudo gluster volume create repvol replica 3 \
  server01:/bricks/rep/repvol \
  server02:/bricks/rep/repvol \
  server03:/bricks/rep/repvol

sudo gluster volume start repvol
```

Информация и состояние:

```bash
sudo gluster volume list
sudo gluster volume info repvol
sudo gluster volume status repvol
sudo gluster volume status repvol detail
```

Остановить и удалить:

```bash
sudo gluster volume stop repvol
sudo gluster volume delete repvol
```

## Клиентское монтирование

На `client`:

```bash
sudo mkdir -p /mnt/repvol
sudo mount -t glusterfs \
  -o xlator-option=transport.address-family=inet,backup-volfile-servers=server02:server03 \
  server01:/repvol /mnt/repvol
```

Проверка:

```bash
findmnt /mnt/repvol
df -hT /mnt/repvol
mount | grep gluster
```

Автоматическое монтирование на клиенте:

```fstab
server01:/repvol /mnt/repvol glusterfs defaults,_netdev,nofail,x-systemd.mount-timeout=90s,xlator-option=transport.address-family=inet,backup-volfile-servers=server02:server03 0 0
```

Размонтировать:

```bash
sudo umount /mnt/repvol
```

Лог ошибки монтирования:

```bash
sudo tail -n 100 /var/log/glusterfs/mnt-repvol.log
```

## Тестовые файлы

```bash
dd if=/dev/urandom of=/mnt/repvol/test-5m.bin bs=1M count=5 status=progress
dd if=/dev/urandom of=/mnt/repvol/test-10m.bin bs=1M count=10 status=progress
ls -lh /mnt/repvol
```

Проверить физические копии replica-тома:

```bash
ssh server01 sudo find /bricks/rep/repvol -maxdepth 1 -type f -ls
ssh server02 sudo find /bricks/rep/repvol -maxdepth 1 -type f -ls
ssh server03 sudo find /bricks/rep/repvol -maxdepth 1 -type f -ls
```

## Self-heal

Постоянное исправление IPv6/IPv4 для пакета openSUSE: [установка и диагностика](deploy/self-heal-ipv4/README.md).

```bash
sudo gluster volume get repvol cluster.self-heal-daemon
sudo gluster volume set repvol cluster.self-heal-daemon enable
sudo gluster volume status repvol
sudo gluster volume heal repvol info
sudo gluster volume heal repvol full
```

## Квоты

```bash
sudo gluster volume quota repvol enable
sudo gluster volume quota repvol limit-usage /limited 10MB
sudo gluster volume quota repvol list
sudo gluster volume quota repvol remove /limited
sudo gluster volume quota repvol disable
```

## Добавление и удаление brick

```bash
sudo gluster volume add-brick distvol server03:/bricks/dist/distvol
sudo gluster volume rebalance distvol start
sudo gluster volume rebalance distvol status

sudo gluster volume remove-brick distvol \
  server03:/bricks/dist/distvol start
sudo gluster volume remove-brick distvol \
  server03:/bricks/dist/distvol status
sudo gluster volume remove-brick distvol \
  server03:/bricks/dist/distvol commit
```

## Снапшоты

```bash
sudo gluster snapshot create snap1 repvol no-timestamp
sudo gluster snapshot list
sudo gluster snapshot info snap1

sudo gluster volume stop repvol
sudo gluster snapshot restore snap1
sudo gluster volume start repvol
```

## Быстрая диагностика

```bash
sudo gluster pool list
sudo gluster peer status
sudo gluster volume info
sudo gluster volume status
sudo gluster volume heal repvol info
sudo lvs -a -o lv_name,lv_size,data_percent,metadata_percent
df -hT /bricks/rep
sudo firewall-cmd --list-ports
```
