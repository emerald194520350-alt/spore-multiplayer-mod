# Beta 3 — 23 сентября 2026 (протокол 4)

## ДНК в общем редакторе

- При получении изменений второго игрока теперь обновляется штатный бюджет
  редактора: установка деталей списывает ДНК, удаление и отмена возвращают её.
  Исправляется сценарий со скриншота: после шипов в одном окне оставалось 6,
  а в другом по-прежнему отображалось 16.
- Симметричная пара считается одной покупкой. Повторный снимок модели и смена
  цвета не списывают ДНК ещё раз. История редактора сохраняет обновлённый бюджет.
- Цена берётся из свойств деталей до загрузки новой модели: её ещё не
  загруженные поля стоимости не используются. При первом входе исходное тело
  не оплачивается повторно; учитываются правки, пришедшие во время перехода.
- Объединённая правка, на которую не хватает ДНК, не применяется: локальная
  модель сохраняется. Одновременные покупки при почти пустом бюджете могут
  потребовать удаления одной детали перед повторной синхронизацией.

## Плавность и анимация

- Положение и полный поворот второго игрока интерполируются каждый кадр с
  буфером 75 мс между сетевыми пакетами. При телепорте, смене тела или разрыве
  связи история сбрасывается. Экстраполяция за последнее известное положение
  не используется; собственное управление остаётся без этого буфера.
- Добавлен экспериментальный запуск штатных анимаций еды и жевания на сетевой
  клетке и визуальной копии гостя. Повторные пакеты не перезапускают один жест
  каждый кадр. Это визуальная передача; пища не начисляется повторно.
- Вызов анимации проверяется по сигнатуре поддерживаемого игрового файла.

## Проверки и установка

Собраны DLL и сервер. Пройдены 94 серверные проверки, 59 верхнеуровневых
нативных проверок с дополнительными сценариями ДНК, симметрии, анимации и
покадрового сглаживания; 33 проверки игрового исполняемого файла, включая
1000 кадров перемещения; проверки исходного кода и 17-секундного простоя сети.
Эти проверки не заменяют игру в двух окнах: новые изменения ДНК, рта и
плавности пока не подтверждены интерактивным игровым тестом.

Закройте оба окна SPORE, распакуйте `spore-multiplayer-mod-beta-3-windows.zip`
и запустите `Start-TwoSpore.ps1`. DLL нужно обновить у обоих игроков.
В релиз также входят все изменения Beta 2: общий мир, рост, задания и детали,
переход в редактор, передача внешности и правок, сглаживание NPC и уведомление
об уходе хоста. Их подробное описание сохранено ниже.

Журнал: `%TEMP%\SporeCoop.Probe.log`; строки `EditorSync: shared budget`
показывают применённую стоимость и остаток ДНК.

---

# Beta 2 — 23 сентября 2026 (протокол 4)

Экспериментальный релиз для клеточного этапа SPORE Galactic Adventures. Обновите
DLL в обоих окнах и сервер одновременно. С Beta 1 / протоколом 3 несовместим.

## Синхронизация игроков и мира

- Перемещение и поворот сетевых клеток используют штатные функции движка,
  которые двигают физические узлы вместе с видимой моделью. Исправлен путь,
  возвращавший клетку к старым координатам после обновления физики.
- При росте учитываются масштаб и смещение мира относительно камеры каждого
  игрока. Игроки и объекты передаются в общей системе координат.
- Передаются полная внешность, цвета, выбранная модель и размер существа.
  Скрытая исходная клетка восстанавливается при потере её визуальной копии.
- Убрано ошибочное включение состояния смерти у сетевых копий. Визуальные
  двойники игроков исключаются из столкновений штатной функцией движка.
- Расширена передача объектов мира: NPC, еда, детали и декорации; добавлены
  здоровье, анимация, состояние смерти, запросы урона и удаления объектов.
  Итоговые изменения мира обрабатывает владелец приглашения.
- Добавлено сглаживание положения и поворота NPC между снимками каждые 100 мс.
  После телепорта или долгого перерыва история сглаживания сбрасывается.

## Общий прогресс и редактор

- Прирост еды, находки и задания учитываются с подтверждением событий, чтобы
  старый снимок не стирал локальное достижение и не начислял его повторно.
- Применение общего прогресса вызывает штатные рост, уведомление о детали,
  обновление задания и первую катсцену. Временные актёры катсцены защищены от
  удаления синхронизацией NPC.
- Второе окно открывает клеточный редактор через штатный переход кампании.
  Общая модель явно применяется после входа, устраняя зелёное стартовое тело.
- Замена модели использует отдельный объект: повторное присоединение текущего
  объекта очищало его тело. История сохраняется после загрузки деталей.
- Завершённые правки отправляются из истории редактора, включая отмену и
  повтор; наведение на ручку детали больше не блокирует передачу.
- Добавлены версии модели, подтверждения и объединение независимых правок.
  Исправлен обнаруженный по журналам сбой повторного приглашения: клиент
  сохранял старую версию, игнорировал новую модель и бесконечно отправлял
  отклоняемые сервером изменения, поэтому шипы оставались в одном окне.

## Выход и запуск

- При выходе или краше хоста второму игроку передаётся причина «Хост вышел»;
  закрытие соединения сохраняет эту причину и очищает сетевое состояние.
- Усилена проверка путей перед обновлением изолированного второго профиля;
  резервные копии получают уникальную временную метку.
- Добавлены журналы готовности редактора, версий, отправленных правок,
  физических копий, движения и проверок совместимости движка.

## Проверки и ограничения

Пройдены 94 проверки серверного протокола, 59 проверок нативного клиента,
32 проверки движка/ABI (включая 1000 перемещений), проверки исходных инвариантов
и 17-секундное подключение без игрового ввода. DLL и сервер собраны заново.
Часть тестов содержит дополнительные проверки сглаживания и объединения моделей.

Последний скриншот подтверждает одинаковое исходное существо в редакторах,
но показывает прежний сбой передачи шипов. Исправление счётчика версий после
нового приглашения проверено автоматическим тестом; его повторная проверка
в двух игровых окнах ещё не выполнена. Полная синхронизация боя, катсцен,
выхода из редактора и переходов между этапами остаётся экспериментальной.
Игра по сети между разными компьютерами и перенос всего мира не проверены.

## Установка

1. Закройте оба окна SPORE.
2. Распакуйте `spore-multiplayer-mod-beta-2-windows.zip`, сохранив папку `bin`.
3. Запустите `Start-TwoSpore.ps1`: он устанавливает DLL для обоих лаунчеров.
4. Загрузите клеточный этап, отправьте приглашение через Esc и примите его.
5. Проверьте рост, подбор детали, вход в редактор и правки в каждом окне.

Требуются SPORE Galactic Adventures, ModAPI Launcher Kit и Windows. Скрипты
подготовлены для двух локальных окон с игрой в `C:\Games\SPORE Collection` и
двумя папками лаунчера в `C:\ProgramData`; на другой установке измените пути.
Нужны также стандартные зависимости Launcher Kit и Visual C++ Runtime x86.
Журнал: `%TEMP%\SporeCoop.Probe.log`. В архив не входят игра, Launcher Kit,
пользовательские сохранения и настройки аккаунтов.

---

# Development history — September 23 Cell progression/editor fix

- Follow-up after the two-window editor report: explicitly install the shared
  body after guest editor activation; a cached creation key alone can fall back
  to the native green starter body. Do not acknowledge the initial snapshot
  before applying it.
- Publish completed native history transactions using the one-based history
  cursor, including undo/redo. A hovered handle no longer blocks sending a
  committed part or receiving an update while the mouse button is released.
  Add per-window `EditorSync` readiness, history and packet diagnostics. These
  follow-up changes pass offline checks; live two-window validation is pending.
- Finish native food/growth, part-unlock/quest notifications and first-part
  cinematic replay instead of only writing shared save counters.
- Open the guest's Cell campaign editor through the verified native entry with
  the complete shared body and paint cached before the transition.
- Replace editor models with a separate loaded object: reattaching the active
  object disposes its own body. Wait for body readiness before publishing or
  committing history, preserve names, and ignore the previous editor visit's
  snapshot while waiting for a new entry acknowledgement.
- Interpolate NPC positions and planar rotations in fixed world coordinates;
  publish every 100 ms, reset on teleports and stop at the last received pose.
  Preserve native cinematic actors instead of deleting them as guest NPCs.
- Deliver and retain the host-left reason before disconnecting the peer.
- Validation: native/protocol regressions, executable fingerprints for the four
  progression/editor entry points, movement/collision fixtures and DLL build.
  A complete two-window growth/unlock/editor gameplay run remains unverified.

# Development history — September 22 replica state fix

- Stop marking synthetic cells as dead (`field_112`), which enables NPC death
  animation and the native `cell_death_continuous` effect. Remove the ineffective
  hiding of structure effect FC1CD6A3.
- Unregister synthetic collision bodies through the verified engine broadphase
  removal routine; retain complete graphics and native body movement.
- Compare the actual avatar's full appearance before creating a guest proxy.
- Add real engine collision-removal regressions and death/collider diagnostics.
  Visual disappearance of dust and guest drift still require session validation.

# Development history — September 21 movement fix

- Move and rotate articulated physics bodies through verified native engine
  routines, fixing the transform-only path that snapped clones back to their
  old physical positions.
- Apply the same fix to guest join placement, guest appearance proxies, and NPC
  mirrors. Reapply existing network bodies just before Cell graphics update.
- Restore the guest's original visibility if its appearance proxy loses graphics.
- Log actual graphics coordinates separately from simulation coordinates.
- Validate movement entry points with ASLR-aware executable fingerprints.
- Add an offline regression using the installed engine's movement routines,
  including 1000 repeated movements. Full gameplay validation is still manual;
  this local build has not been published as a new release.
- Keep launcher creation-library backups timestamped even when the source has
  no Games directory; validate isolated replacement paths before removal.

# spore multiplayer mod beta 1

Experimental Windows build for SPORE Galactic Adventures Cell Stage. Both game
instances and the session server must use this release (protocol 3).

## Changes

- Either window can own the invitation, creature appearance, scale and shared pause.
- Fixed invisible peers when a cell is drawn through structure attachments.
- Native cells use simulation coordinates and scale without applying a nested model's scale twice.
- Removed redundant local appearance proxies for identical creatures and the blocking standalone-bake requirement.
- NPC replication carries the selected creature model, with the nearest 48 creatures included in each snapshot.
- NPC and player-clone removal uses complete engine cleanup, guarded for the supported executable ABI.
- Independent, acknowledged food gains and shared part unlocks survive simultaneous pickups and delayed snapshots.
- Movement traffic no longer evicts queued inventory or invitation events.
- New invitations reset progress acknowledgements; reconnects restore local appearance and release the mod's pause.
- The launcher updates both DLLs and the server together and refuses to mix builds while old windows are running.

## Install and test

1. Install SPORE Galactic Adventures and Spore ModAPI Launcher Kit first.
2. Close both game windows before replacing an older mod version.
3. Extract `spore-multiplayer-mod-beta-1-windows.zip` into one folder, preserving `bin/`.
4. Run `Start-TwoSpore.ps1`, or `Start-TwoSpore.ps1 -TraceMovement` for diagnostics.
5. Load a Cell Stage save in one window, invite the other player from Esc, and accept in the second window.

The supplied local launcher targets the tested installation at
`C:\Games\SPORE Collection` and two Launcher Kit folders under `C:\ProgramData`.
Adjust the launcher paths for another installation. It backs up and refreshes
the second profile from the first before starting both games. The game and the
Launcher Kit are not included in the release archive.

## Validation and known issues

The DLL and server build successfully. Automated validation passed 84 server
protocol assertions, 53 native assertions, source invariants, and a 17-second
native-client idle connection test. Both windows were launched with the release
DLL. Full gameplay validation remains in progress; these tests do not drive the
SPORE simulation. A live two-window smoke test shows matching shared food/part
counters, but native peer cells can still drift away from received coordinates
and NPC replication needs further in-game validation. Movement should therefore
be treated as experimental, not as fully resolved in this beta.

NPC health, combat, food objects and the entire world simulation are not yet
authoritative across both windows. Mirrored NPCs in the joining window remain
invulnerable. Other stages and editor transitions need further gameplay testing.
Cross-PC Radmin VPN play and complete world transfer are not validated. Network
cell creation is disabled if the engine cleanup signature is unsupported.

Diagnostics: `%TEMP%\SporeCoop.Probe.log`. The repository contains the source and
build scripts; the Windows archive contains the compiled mod, server and launch
scripts, without game saves or private configuration.
