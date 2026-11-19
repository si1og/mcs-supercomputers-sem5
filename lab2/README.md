# Лабораторная работа №2: GlusterFS

Краткая шпаргалка без теории: [COMMANDS.md](COMMANDS.md).

## 1. Текущая конфигурация стенда

В лабораторной используются четыре виртуальные машины под управлением openSUSE Leap 16.0:

| Узел | IP-адрес | Роль |
|---|---|---|
| `server01` | `192.168.100.11` | сервер GlusterFS |
| `server02` | `192.168.100.12` | сервер GlusterFS |
| `server03` | `192.168.100.13` | сервер GlusterFS |
| `client` | `192.168.100.14` | клиент GlusterFS |

На каждом сервере присутствует отдельный пустой диск `/dev/sdb` объёмом 20 ГБ. Служба `glusterd` запущена на всех трёх серверах, имена узлов разрешаются через внутреннюю сеть, установлена версия GlusterFS 11.1.

`firewalld` на узлах включён.

## 2. Основные термины

### Trusted storage pool

Trusted storage pool — объединение серверов GlusterFS. Узлы обмениваются конфигурацией томов и считают друг друга доверенными.

### Brick

Brick, или «кирпичик», — каталог на сервере, в котором физически хранятся данные GlusterFS. Brick записывается в формате `hostname:/path`, например:

```text
server01:/bricks/rep/repvol
```

Brick является минимальной единицей хранения GlusterFS.

### Volume

Volume, или том GlusterFS, — логическое распределённое хранилище, собранное из одного или нескольких brick. Клиент монтирует volume, а не отдельный brick.

### glusterd

`glusterd` — управляющий демон GlusterFS. Он отвечает за trusted pool, конфигурацию томов и запуск процессов brick.

### glusterfsd

`glusterfsd` — процесс, обслуживающий конкретный brick и операции с его файлами.

### LVM

В этой работе LVM используется для подготовки дискового пространства под brick:

- **PV (Physical Volume)** — физическое блочное устройство, например `/dev/sdb`;
- **VG (Volume Group)** — группа дискового пространства;
- **thin pool** — пул, из которого динамически выделяется место;
- **thin LV** — тонкий логический том, на котором создаётся файловая система для brick.

Необходимо различать LVM-том и том GlusterFS:

```text
/dev/sdb
  └─ LVM VG vg_gluster
       └─ thin pool
            └─ LVM LV lv_rep
                 └─ XFS
                      └─ /bricks/rep
                           └─ brick
                                └─ GlusterFS volume repvol
```

LVM LV — локальное блочное устройство. GlusterFS volume — распределённая файловая система поверх каталогов на нескольких серверах.

## 3. Типы томов GlusterFS

### Distributed

Каждый файл целиком размещается на одном из brick. Общая ёмкость равна сумме ёмкостей brick, но резервирования нет. При отказе сервера файлы, находившиеся на нём, становятся недоступны.

### Replica

Каждый файл полностью копируется на несколько brick. В `replica 3` хранятся три полные копии файла.

### Arbiter 2+1

Два brick хранят полные копии данных, третий brick хранит в основном метаданные. Арбитр помогает определить правильную копию и избежать split-brain, но не является третьей полной копией содержимого.

### Distributed-replicated

Том состоит из нескольких replica-наборов. Файлы распределяются между наборами, а внутри каждого набора реплицируются.

### Dispersed

Данные разбиваются на фрагменты с добавлением избыточности по принципу erasure coding. Такой том похож по идее на RAID 5/6 и экономнее полной репликации.

## 4. Подготовка thin LVM

> **Внимание:** следующие команды уничтожат содержимое `/dev/sdb`. Перед выполнением необходимо убедиться, что выбран правильный пустой диск.

На каждом из серверов `server01`, `server02`, `server03` проверить диск:

```bash
lsblk -f /dev/sdb
sudo wipefs -n /dev/sdb
```

Установить LVM и инструменты XFS:

```bash
sudo zypper install -y lvm2 xfsprogs
```

Создать physical volume, volume group и thin pool:

```bash
sudo pvcreate /dev/sdb
sudo vgcreate vg_gluster /dev/sdb
sudo lvcreate -L 18G --thinpool thinpool vg_gluster
```

Проверить результат:

```bash
sudo pvs
sudo vgs
sudo lvs -a
```

Thin pool создаётся сразу, потому что для снапшотов GlusterFS brick должны находиться на поддерживаемом snapshot-хранилище. Это позволит не переделывать дисковую схему в конце лабораторной.

## 5. Создание файловой системы для первого brick

На каждом из трёх серверов создать thin LV объёмом 4 ГБ:

```bash
sudo lvcreate -V 4G -T vg_gluster/thinpool -n lv_rep
sudo mkfs.xfs -f -i size=512 /dev/vg_gluster/lv_rep
```

Создать точку монтирования и смонтировать файловую систему:

```bash
sudo mkdir -p /bricks/rep
sudo mount /dev/vg_gluster/lv_rep /bricks/rep
sudo mkdir -p /bricks/rep/repvol
```

Здесь:

- `/bricks/rep` — точка монтирования локальной XFS;
- `/bricks/rep/repvol` — каталог, который будет передан GlusterFS как brick тома `repvol`.

Узнать UUID файловой системы:

```bash
sudo blkid /dev/vg_gluster/lv_rep
```

Добавить на каждом сервере в `/etc/fstab`, подставив UUID соответствующего узла:

```fstab
UUID=<UUID> /bricks/rep xfs defaults,noatime 0 2
```

Проверить автоматическое монтирование:

```bash
sudo umount /bricks/rep
sudo mount -a
findmnt /bricks/rep
df -h /bricks/rep
```

## 6. Настройка firewall

Полностью отключать firewall не требуется. Начиная с GlusterFS 10 порты brick выбираются случайно между `base-port` и `max-port` из `/etc/glusterfs/glusterd.vol`. Чтобы не открывать стандартный диапазон `49152–60999`, на всех трёх серверах следует явно задать одинаковый ограниченный диапазон и IPv4:

```text
volume management
    ...
    option transport.address-family inet
    option base-port 49152
    option max-port 49251
end-volume
```

Явное `transport.address-family inet` обязательно для нашего стенда: пакет openSUSE по умолчанию пытался разрешать имена узлов через IPv6, из-за чего peers переходили в `Disconnected`, а в `glusterd.log` появлялось `DNS resolution failed` с `family=10`.

После изменения `glusterd.vol` перезапустить `glusterd` на каждом сервере:

```bash
sudo systemctl restart glusterd
```

Если тома и brick-процессы уже запущены на старых случайных портах, том сначала следует остановить, после перезапуска `glusterd` снова запустить и проверить новые порты:

```bash
sudo gluster volume stop repvol
# Перезапустить glusterd на server01, server02 и server03.
sudo gluster volume start repvol
sudo gluster volume status repvol
```

На `server01`, `server02` и `server03` открыть управляющие порты GlusterFS и тот же диапазон портов brick:

```bash
sudo firewall-cmd --permanent --add-port=24007-24008/tcp
sudo firewall-cmd --permanent --add-port=49152-49251/tcp
sudo firewall-cmd --reload
```

Проверить правила:

```bash
sudo firewall-cmd --list-ports
```

Доступ к этим портам должен предоставляться только из лабораторной сети `192.168.100.0/24`.

## 7. Создание trusted storage pool

На `server01` добавить остальные серверы:

```bash
sudo /usr/sbin/gluster peer probe server02
sudo /usr/sbin/gluster peer probe server03
```

Проверить состояние пула:

```bash
sudo /usr/sbin/gluster peer status
sudo /usr/sbin/gluster pool list
```

Для `server02` и `server03` ожидается состояние:

```text
Peer in Cluster (Connected)
```

`server01` не показывается как peer самому себе — это нормально.

Проверку необходимо выполнить на каждом сервере. Если один сервер видит peers как `Connected`, это ещё не гарантирует согласованное состояние всего пула:

```bash
ssh server01 sudo gluster pool list
ssh server02 sudo gluster pool list
ssh server03 sudo gluster pool list
```

Удаление узла из пула выполняется командой:

```bash
sudo /usr/sbin/gluster peer detach <hostname>
```

Перед удалением узла необходимо убрать из томов все находящиеся на нём brick.

## 8. Создание replica-тома

На `server01` создать том с тремя полными репликами:

```bash
sudo /usr/sbin/gluster volume create repvol replica 3 \
  server01:/bricks/rep/repvol \
  server02:/bricks/rep/repvol \
  server03:/bricks/rep/repvol
```

Запустить том:

```bash
sudo /usr/sbin/gluster volume start repvol
```

Проверить конфигурацию и процессы brick:

```bash
sudo /usr/sbin/gluster volume info repvol
sudo /usr/sbin/gluster volume status repvol
```

Параметр `force` не должен требоваться, поскольку каждый brick расположен на выделенной файловой системе. Если GlusterFS требует `force`, сначала необходимо выяснить причину.

## 9. Монтирование тома на клиенте

На `client` создать точку монтирования:

```bash
sudo mkdir -p /mnt/repvol
```

Смонтировать том:

```bash
sudo mount -t glusterfs \
  -o xlator-option=transport.address-family=inet,backup-volfile-servers=server02:server03 \
  server01:/repvol /mnt/repvol
```

Проверить, что каталог действительно является монтированием GlusterFS:

```bash
findmnt /mnt/repvol
df -h /mnt/repvol
```

Создать тестовый файл:

```bash
echo "hello from client" | sudo tee /mnt/repvol/test.txt
cat /mnt/repvol/test.txt
```

Для автоматического монтирования добавить на клиенте в `/etc/fstab`:

```fstab
server01:/repvol /mnt/repvol glusterfs defaults,_netdev,xlator-option=transport.address-family=inet,backup-volfile-servers=server02:server03 0 0
```

Проверить наличие физической копии файла на серверах:

```bash
ssh server01 sudo ls -la /bricks/rep/repvol
ssh server02 sudo ls -la /bricks/rep/repvol
ssh server03 sudo ls -la /bricks/rep/repvol
```

У `replica 3` файл должен находиться на всех трёх brick.

## 10. Как GlusterFS обрабатывает запись

При монтировании `server01:/repvol` происходят следующие действия:

1. Клиент обращается к `glusterd` на `server01` и получает описание тома — volfile.
2. В volfile перечислены brick и цепочка внутренних модулей-translator.
3. Клиент подключается непосредственно к процессам `glusterfsd`, обслуживающим brick.
4. `server01` после выдачи конфигурации не является центральным прокси для всех данных.
5. Translator AFR отправляет запись на реплики.
6. GlusterFS хранит служебное состояние в extended attributes локальной файловой системы.
7. После возвращения недоступного brick механизм heal сравнивает состояние реплик и восстанавливает отставшую копию.

Таким образом, `glusterd` отвечает преимущественно за управление, а данные передаются напрямую между клиентом и серверами хранения.

## 11. Подготовка brick для других типов томов

Для каждого нового типа тома необходимо использовать отдельный thin LV, отдельную файловую систему и отдельный каталог brick. Один brick нельзя одновременно включать в несколько томов.

В этой лабораторной используется единое соглашение: последний компонент пути brick совпадает с именем GlusterFS-тома и оканчивается на `vol`:

```text
repvol  → /bricks/rep/repvol
distvol → /bricks/dist/distvol
arbvol  → /bricks/arb/arbvol
```

Так по пути сразу видно, какому тому принадлежит каталог. Само имя каталога для GlusterFS технически может быть любым.

Пример подготовки brick для distributed-тома на нужном сервере:

```bash
sudo lvcreate -V 3G -T vg_gluster/thinpool -n lv_dist
sudo mkfs.xfs -f -i size=512 /dev/vg_gluster/lv_dist
sudo mkdir -p /bricks/dist
sudo mount /dev/vg_gluster/lv_dist /bricks/dist
sudo mkdir -p /bricks/dist/distvol
```

Для этой файловой системы также необходимо добавить запись по UUID в `/etc/fstab`.

### Distributed

Подготовить `/bricks/dist/distvol` на `server01` и `server02`, затем выполнить:

```bash
sudo gluster volume create distvol \
  server01:/bricks/dist/distvol \
  server02:/bricks/dist/distvol

sudo gluster volume start distvol
```

Файлы будут распределяться между серверами, но не реплицироваться.

### Replica 3

```bash
sudo gluster volume create repvol replica 3 \
  server01:/bricks/rep/repvol \
  server02:/bricks/rep/repvol \
  server03:/bricks/rep/repvol

sudo gluster volume start repvol
```

### Replica с арбитром

Подготовить отдельный `/bricks/arb/arbvol` на всех трёх серверах:

```bash
sudo gluster volume create arbvol replica 3 arbiter 1 \
  server01:/bricks/arb/arbvol \
  server02:/bricks/arb/arbvol \
  server03:/bricks/arb/arbvol

sudo gluster volume start arbvol
```

Первые два brick хранят данные, последний brick является арбитром.

## 12. Добавление brick и rebalance

При добавлении brick в distributed-том новые файлы начинают распределяться с учётом нового brick, но старые файлы автоматически не перемещаются.

Добавить brick:

```bash
sudo gluster volume add-brick distvol \
  server03:/bricks/dist/distvol
```

Запустить перераспределение данных:

```bash
sudo gluster volume rebalance distvol start
sudo gluster volume rebalance distvol status
```

`rebalance` вычисляет новое размещение файлов и переносит те файлы, для которых после изменения топологии изменился целевой brick.

Для replica-тома brick добавляются полным replica-набором. Например, добавление ещё одного набора `replica 3` требует три новых brick — по одному на каждом сервере.

## 13. Удаление brick

Для distributed-тома удаление выполняется в несколько этапов:

```bash
sudo gluster volume remove-brick distvol \
  server03:/bricks/dist/distvol start
```

Проверить перенос данных:

```bash
sudo gluster volume remove-brick distvol \
  server03:/bricks/dist/distvol status
```

После успешного завершения миграции подтвердить удаление:

```bash
sudo gluster volume remove-brick distvol \
  server03:/bricks/dist/distvol commit
```

Нельзя выполнять `commit`, пока перенос данных не завершился успешно.

## 14. Heal

Проверить, запущены ли self-heal daemon на всех серверах:

```bash
sudo gluster volume status repvol
sudo gluster volume get repvol cluster.self-heal-daemon
```

Если self-heal выключен, включить его явно:

```bash
sudo gluster volume set repvol cluster.self-heal-daemon enable
```

Проверить состояние реплик:

```bash
sudo gluster volume heal repvol info
```

Запустить полное восстановление:

```bash
sudo gluster volume heal repvol full
```

В нормальной работе heal запускается автоматически. `full` полезен для лабораторной демонстрации и после существенного изменения топологии.

## 15. Квоты

Включить квоты для тома:

```bash
sudo gluster volume quota repvol enable
```

Создать через клиентское монтирование каталог:

```bash
sudo mkdir -p /mnt/repvol/limited
```

Ограничить каталог размером 10 МБ. Путь задаётся относительно корня тома:

```bash
sudo gluster volume quota repvol limit-usage /limited 10MB
sudo gluster volume quota repvol list
```

Проверить квоту:

```bash
sudo dd if=/dev/zero of=/mnt/repvol/limited/file1 bs=1M count=5
sudo dd if=/dev/zero of=/mnt/repvol/limited/file2 bs=1M count=8
```

Вторая запись должна завершиться ошибкой `Disk quota exceeded`.

Удалить ограничение и отключить квоты:

```bash
sudo gluster volume quota repvol remove /limited
sudo gluster volume quota repvol disable
```

## 16. Снапшоты

Снапшот GlusterFS фиксирует состояние тома на определённый момент. Фактические snapshot-операции выполняются нижележащим LVM, поэтому brick были заранее размещены на thin LV.

Создать тестовый файл через клиентское монтирование:

```bash
echo "version 1" | sudo tee /mnt/repvol/document.txt
```

Создать снапшот:

```bash
sudo gluster snapshot create snap1 repvol no-timestamp
sudo gluster snapshot list
sudo gluster snapshot info snap1
```

Изменить данные после создания снапшота:

```bash
echo "version 2" | sudo tee /mnt/repvol/document.txt
echo "new file" | sudo tee /mnt/repvol/new.txt
```

Перед восстановлением необходимо размонтировать клиентский том и остановить volume:

```bash
sudo umount /mnt/repvol
sudo gluster volume stop repvol
sudo gluster snapshot restore snap1
sudo gluster volume start repvol
sudo mount /mnt/repvol
```

После восстановления `document.txt` должен вернуться к первой версии, а `new.txt` исчезнуть.

## 17. Удаление тома

Сначала размонтировать его на клиентах:

```bash
sudo umount /mnt/repvol
```

Затем на сервере управления остановить и удалить том:

```bash
sudo gluster volume stop repvol
sudo gluster volume delete repvol
```

Команда `volume delete` удаляет конфигурацию тома, но не очищает содержимое brick. В каталоге остаются файлы, `.glusterfs` и extended attributes. Повторно использовать такой каталог без очистки нельзя.

Удалять содержимое brick можно только после проверки, что том действительно удалён и данные больше не нужны.

## 18. Рекомендуемый порядок выполнения лабораторной

1. Подготовить thin LVM и XFS на трёх серверах.
2. Создать trusted storage pool.
3. Создать и проверить `replica 3`.
4. Создать и исследовать distributed-том.
5. Создать и исследовать replica-том с арбитром.
6. Добавить brick и выполнить `rebalance`.
7. Удалить brick с предварительной миграцией данных.
8. Проверить `heal`.
9. Настроить и проверить квоты.
10. Создать и восстановить снапшот.
11. Дополнительно исследовать distributed-replicated, dispersed или sharding.

## 19. Важные предостережения

- Не записывать пользовательские файлы напрямую в `/bricks/.../<volume-name>`. Работать следует через клиентское монтирование.
- Не удалять физические файлы из brick при обычной работе.
- Не использовать корневую файловую систему как brick, если доступен отдельный диск.
- Не использовать один brick в нескольких томах.
- После добавления brick в distributed-том запускать `rebalance`.
- После `remove-brick ... start` проверять `status` и только затем выполнять `commit`.
- Не отключать firewall полностью без необходимости.
- Перед созданием файловой системы повторно проверять имя диска.
- Следить за заполнением thin pool:

```bash
sudo lvs -a -o lv_name,lv_size,data_percent,metadata_percent
```

При заполнении `Data%` или `Meta%` thin pool тома могут перейти в аварийное состояние, даже если виртуальный размер отдельных thin LV выглядит достаточным.

## 20. Вопросы и практические задания на защите

Ниже собраны вопросы из заметок студентов. Формулировки приведены в нормальный вид, а команды адаптированы к нашему стенду: три сервера `server01`–`server03` и отдельный узел `client`.

### 20.1. Какие узлы входят в trusted storage pool

Проверить список узлов можно с любого сервера GlusterFS:

```bash
sudo gluster pool list
sudo gluster peer status
```

В trusted pool должны входить только серверы хранения:

```text
server01
server02
server03
```

Узел `client` в пул входить не должен. Он только получает конфигурацию тома и подключается к brick-процессам. Команда `peer status`, запущенная на одном из серверов, обычно показывает два других сервера; текущий сервер не отображается как peer самому себе.

Для каждого peer следует проверить состояние:

```text
Peer in Cluster (Connected)
```

### 20.2. Как серверы связаны между собой

Сначала проверяется разрешение имён и обычная IP-связность:

```bash
getent hosts server01 server02 server03 client
ping -c 2 server02
ping -c 2 server03
```

Затем можно посмотреть соединения управляющих демонов:

```bash
ss -tn | grep 24007
```

Порт `24007/tcp` используется `glusterd` для управления кластером. После создания томов клиент и серверы также соединяются с процессами brick на портах из настроенного brick-диапазона.

Узлы объединяются командами, выполняемыми, например, на `server01`:

```bash
sudo gluster peer probe server02
sudo gluster peer probe server03
```

Команда `peer probe` устанавливает доверие между серверами и добавляет сведения об узле в конфигурацию trusted pool.

### 20.3. Какие пакеты устанавливаются на узлы

В Debian/Ubuntu пакеты обычно разделены:

```text
glusterfs-server — серверная часть;
glusterfs-client — FUSE-клиент и средство монтирования.
```

В используемой нами openSUSE Leap 16.0 пакет `glusterfs` содержит как серверные, так и клиентские компоненты, включая:

```text
/usr/sbin/glusterd
/usr/sbin/glusterfs
/usr/sbin/mount.glusterfs
```

Роль машины определяется не только установленным пакетом, но и её настройкой:

- на `server01`–`server03` запущен `glusterd` и размещаются brick;
- на `client` `glusterd` не нужен, используется только `mount.glusterfs`.

Проверить пакеты и службы можно командами:

```bash
rpm -qa | grep -i gluster
systemctl status glusterd
```

На клиенте состояние `glusterd` `inactive` является нормальным.

### 20.4. Как вывести список и информацию по всем томам

Список томов:

```bash
sudo gluster volume list
```

Информация обо всех томах:

```bash
sudo gluster volume info
```

Информация о конкретном томе:

```bash
sudo gluster volume info distvol
```

Состояние процессов всех томов:

```bash
sudo gluster volume status
```

Подробное состояние и свободное место на brick конкретного тома:

```bash
sudo gluster volume status distvol detail
```

### 20.5. Как работает distributed-том

В distributed-томе каждый файл целиком хранится только на одном brick. GlusterFS применяет DHT-хеширование к имени и пути файла и выбирает целевой brick.

Например:

```text
file1.dat → server02:/bricks/dist/distvol
file2.dat → server01:/bricks/dist/distvol
file3.dat → server03:/bricks/dist/distvol
```

Один файл не разбивается на части между brick. Разные файлы распределяются между серверами. Поэтому ёмкости brick складываются, но резервной копии файла нет: при недоступности brick размещённые на нём файлы также становятся недоступны.

Распределение определяется хешем, поэтому нельзя заранее гарантировать, что два созданных файла окажутся на разных узлах. Для наглядного эксперимента лучше создать несколько файлов.

### 20.6. Как найти точку монтирования тома на клиенте

На `client`:

```bash
findmnt -t fuse.glusterfs
df -hT | grep gluster
mount | grep gluster
```

Перед созданием тестовых файлов необходимо обязательно убедиться, что каталог действительно является точкой монтирования GlusterFS. Иначе файлы будут записаны на локальный системный диск клиента.

Пример ожидаемого результата:

```text
server01:/distvol on /mnt/distvol type fuse.glusterfs
```

### 20.7. Как создать файлы заданного размера

Команда `dd` имеет основные параметры:

- `if` — input file, источник данных;
- `of` — output file, создаваемый файл;
- `bs` — block size, размер одного блока ввода-вывода;
- `count` — количество блоков.

Размер выходного файла без учёта короткого чтения определяется формулой:

```text
размер файла = bs × count
```

Создать файл размером 4 МиБ:

```bash
dd if=/dev/urandom of=/mnt/distvol/test-4m.bin \
  bs=1M count=4 status=progress
```

Создать файлы размером 5 и 10 МиБ:

```bash
dd if=/dev/urandom of=/mnt/distvol/test-5m.bin \
  bs=1M count=5 status=progress

dd if=/dev/urandom of=/mnt/distvol/test-10m.bin \
  bs=1M count=10 status=progress
```

Если на защите требуют размер блока 100 КиБ, можно использовать:

```bash
dd if=/dev/urandom of=/mnt/distvol/test-5m.bin \
  bs=100K count=50 status=progress

dd if=/dev/urandom of=/mnt/distvol/test-10m.bin \
  bs=100K count=100 status=progress
```

В GNU `dd` суффикс `K` означает 1024 байта, поэтому `100K × 50 = 5 120 000` байт, то есть примерно 4,88 МиБ. Для ровно 5 МиБ удобнее использовать `bs=1M count=5`.

`/dev/urandom` создаёт файл со случайным содержимым. Для быстрого создания файла, заполненного нулями, можно использовать `/dev/zero`:

```bash
dd if=/dev/zero of=/mnt/distvol/test.bin \
  bs=1M count=5 status=progress
```

### 20.8. Как посмотреть распределение файлов по brick

Файлы всегда создаются через клиентское монтирование:

```bash
ls -lh /mnt/distvol
```

Затем на серверах проверяется физическое содержимое brick:

```bash
ssh server01 sudo find /bricks/dist/distvol -maxdepth 1 -type f -ls
ssh server02 sudo find /bricks/dist/distvol -maxdepth 1 -type f -ls
ssh server03 sudo find /bricks/dist/distvol -maxdepth 1 -type f -ls
```

Для distributed-тома каждый файл должен физически находиться на одном из brick. Служебный каталог `.glusterfs` в выводе не является пользовательским файлом.

Для replica-тома один и тот же файл будет присутствовать на каждом brick соответствующего replica-набора. Для arbiter-тома на арбитре присутствуют метаданные и файл нулевого размера, но не пользовательское содержимое.

Напрямую редактировать или удалять файлы внутри brick нельзя. Просмотр выполняется только для демонстрации физического размещения.

### 20.9. Как проверить квоты

На сервере управления вывести все ограничения тома:

```bash
sudo gluster volume quota repvol list
```

Вывод показывает:

- путь относительно корня тома;
- hard limit;
- soft limit;
- использованный объём;
- доступный остаток.

Например, ограничить каталог `/limited` десятью мегабайтами:

```bash
sudo gluster volume quota repvol enable
sudo gluster volume quota repvol limit-usage /limited 10MB
sudo gluster volume quota repvol list
```

На клиенте создать два файла суммарным размером больше квоты:

```bash
dd if=/dev/zero of=/mnt/repvol/limited/file-5m.bin \
  bs=1M count=5 status=progress

dd if=/dev/zero of=/mnt/repvol/limited/file-8m.bin \
  bs=1M count=8 status=progress
```

Первая запись должна завершиться успешно, а при записи второго файла ожидается ошибка:

```text
Disk quota exceeded
```

Клиент видит результат действия квоты, но административная информация о лимитах проверяется командой `gluster volume quota ... list` на сервере.

### 20.10. Как показать работу снапшотов

Для GlusterFS snapshot brick должны располагаться на поддерживаемом snapshot-хранилище. В нашей схеме это XFS на thin LV из LVM thin pool.

Последовательность демонстрации:

1. Создать файл через клиентское монтирование.
2. Создать снапшот.
3. Изменить исходный файл и добавить новые файлы.
4. Остановить том и восстановить снапшот.
5. Снова запустить и смонтировать том.
6. Проверить, что вернулось старое состояние.

Создать исходные данные:

```bash
echo 'version 1' | sudo tee /mnt/repvol/document.txt
```

Создать снапшот и вывести информацию о нём:

```bash
sudo gluster snapshot create snap1 repvol no-timestamp
sudo gluster snapshot list
sudo gluster snapshot info snap1
```

Изменить состояние тома после снимка:

```bash
echo 'version 2' | sudo tee /mnt/repvol/document.txt
echo 'created after snapshot' | sudo tee /mnt/repvol/new.txt
```

Перед восстановлением размонтировать том на клиенте:

```bash
sudo umount /mnt/repvol
```

На сервере управления остановить том и восстановить снапшот:

```bash
sudo gluster volume stop repvol
sudo gluster snapshot restore snap1
sudo gluster volume start repvol
```

Снова смонтировать том на клиенте:

```bash
sudo mount -t glusterfs \
  -o xlator-option=transport.address-family=inet \
  server01:/repvol /mnt/repvol
cat /mnt/repvol/document.txt
ls -la /mnt/repvol
```

После восстановления ожидается:

- `document.txt` снова содержит `version 1`;
- файл `new.txt` отсутствует;
- использованный для восстановления снапшот удалён GlusterFS автоматически.

Снапшот создаётся не простым копированием всех файлов. LVM thin snapshot сначала использует общее с исходным томом состояние блоков, а при последующих изменениях сохраняет различия по механизму copy-on-write.

### 20.11. Какие характеристики можно запросить у системы

Для описания стенда полезно вывести:

```bash
hostnamectl
uname -a
lscpu
free -h
lsblk -f
ip -br addr
df -hT
```

Для подсистемы памяти особенно полезны:

```bash
free -h
lscpu | grep -i cache
sudo dmidecode --type memory
```

Полученные характеристики следует сравнивать с параметрами, заданными виртуальной машине в гипервизоре, и с ожидаемыми значениями из расчётов.

Заметки про влияние «размера поля» и реализацию граничных условий, вероятнее всего, относятся к другой вычислительной лабораторной, а не к GlusterFS. В контексте этой работы размер блока `dd` влияет главным образом на число операций ввода-вывода и производительность, но при одинаковом произведении `bs × count` не меняет итоговый логический размер файла.

### 20.12. Короткий сценарий демонстрации всей работы

На защите можно придерживаться следующего порядка:

```bash
# 1. Показать trusted pool
sudo gluster pool list
sudo gluster peer status

# 2. Показать тома и их устройство
sudo gluster volume list
sudo gluster volume info
sudo gluster volume status

# 3. На клиенте показать монтирование
findmnt -t fuse.glusterfs

# 4. Создать файлы в distributed-томе
dd if=/dev/urandom of=/mnt/distvol/test-5m.bin bs=1M count=5
dd if=/dev/urandom of=/mnt/distvol/test-10m.bin bs=1M count=10

# 5. Посмотреть их физическое размещение на серверах
ssh server01 sudo find /bricks/dist/distvol -maxdepth 1 -type f -ls
ssh server02 sudo find /bricks/dist/distvol -maxdepth 1 -type f -ls
ssh server03 sudo find /bricks/dist/distvol -maxdepth 1 -type f -ls

# 6. Показать квоты
sudo gluster volume quota repvol list

# 7. Показать снапшоты
sudo gluster snapshot list
sudo gluster snapshot info
```

При объяснении важно не просто показать команды, а сформулировать вывод:

- клиент не входит в trusted pool;
- distributed распределяет целые файлы, но не создаёт резервных копий;
- replica хранит несколько полных копий;
- квота ограничивает объём записи в заданный путь;
- snapshot возвращает состояние тома к определённому моменту;
- GlusterFS объединяет каталоги brick, а LVM предоставляет им локальное управляемое хранилище.
