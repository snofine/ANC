# ANCApp — Real-Time Active Noise Cancellation MVP

> ⚡ **Built, architected, and DSP-optimized with OpenAI Codex**

ANCApp is an experimental real-time Active Noise Cancellation (ANC) engine designed for single boom-mic USB headsets. It tackles the fundamental physics problem: testing phase inversion latency and attenuating consistent low-frequency tones directly at the ear.

### 🔬 Core Architecture
* **Low-Latency Audio Processing**: Real-time phase shift & FIR/IIR filter pipeline.
* **Hardware Target**: Tuned for USB boom-microphones with strict latency benchmarks.
* **Safety First**: Conservative gain stages to avoid acoustic feedback loops.

---


Это первый этап системы активного шумоподавления для USB-гарнитуры с одним boom-микрофоном. Он предназначен для проверки одного физического вопроса: можно ли при конкретной гарнитуре, посадке и задержке ослабить стабильный низкочастотный тон у уха.

Это **не средство защиты слуха** и не готовый аналог аппаратного ANC. Во время обычной работы микрофона у уха нет, поэтому приложение не может измерять реальное остаточное давление и не показывает вымышленное значение подавления в dB.

## Что уже реализовано

- Windows 10/11 x64, C++20, только системные Windows API;
- выбор capture/render endpoint по индексу;
- WASAPI Shared event-driven audio, `IAudioClient3` low-latency period с fallback;
- 32-bit float DSP; преобразование WASAPI float и PCM 16/24/32;
- mono reference из одно- или многоканального capture endpoint;
- lock-free SPSC ring buffer без выделений памяти в realtime-тракте;
- DC blocker, HPF и LPF (по умолчанию 40–150 Гц);
- инверсия полярности, ручная дополнительная фаза и задержка отдельно для L/R;
- независимый gain L/R и общий плавный strength ramp;
- hard ceiling `-12 dBFS` с мягким ограничением перед ним;
- простое обнаружение голоса и импульса; импульс временно уменьшает ANC;
- консервативный runaway detector по быстрому/медленному росту энергии;
- мгновенная фиксация Emergency Stop с быстрым бесщелчковым спадом;
- автоматическая остановка ANC при потере микрофона;
- MMCSS `Pro Audio` для capture/render потоков;
- статистика RMS, period, WASAPI latency и overruns/underruns;
- автономные тесты базового DSP.

WASAPI Shared выбран намеренно: anti-noise поток смешивается Windows Audio Engine с музыкой и играми. В MVP приложение не устанавливает виртуальный аудиодрайвер и не захватывает обычный программный звук.

## Сборка

Нужны Visual Studio 2022 Build Tools с компонентами «Desktop development with C++» и CMake 3.24+.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

В текущем окружении репозитория C++ toolchain отсутствует, поэтому бинарник здесь не создаётся. Проект не скачивает зависимости.

## Первый безопасный запуск

Сначала посмотрите индексы устройств:

```powershell
.\build-vs\Release\anc_app.exe --list
```

Перед включением **снимите гарнитуру** и установите низкую системную громкость. Для тестового тона 100 Гц:

```powershell
.\build-vs\Release\anc_app.exe --input 0 --output 1 --frequency 100 --strength 5 --start
```

Экспериментальный максимально широкий режим для этой конструкции:

```powershell
.\build-vs\Release\anc_app.exe --input 0 --output 1 --experimental-wideband --strength 10 --start
```

Он расширяет reference path до 20–500 Гц и разрешает вручную поднимать strength до 100%. Hard limiter `-12 dBFS`, impulse ducking, runaway detector и Emergency Stop остаются включёнными. Режим не способен надёжно подавлять весь слышимый диапазон: без микрофонов возле ушей фаза выше 500 Гц неизвестна.

Параметр `--start` обязателен: без него приложение остаётся в безопасном режиме и ничего не выводит.

Клавиши во время работы:

- `X` — Emergency Stop;
- `R` — повторное включение после проверки причины;
- `+` / `-` — strength с шагом 1%, максимум 30% в штатном или 100% в experimental wideband;
- `Q` — выход;
- `Ctrl+Shift+F12` — системный Emergency Stop; если Windows не даст зарегистрировать сочетание, приложение предупредит и останется клавиша `X`.

Нулевая дополнительная фаза уже означает инверсию полярности. `--phase-left` и `--phase-right` добавляют только причинную задержку для выбранной `--frequency`; программа не может «заглянуть в будущее». Для широкополосного шума ручная фазовая настройка не является корректной заменой FIR-модели.

## Протокол измерения 100 Гц

1. Не надевая гарнитуру, проверьте отсутствие громкого сигнала и clipping.
2. Разместите микрофон в точке левого уха внутри чашки.
3. Запишите уровень при ANC OFF, не меняя источник, громкость и положение.
4. Запустите с strength 5%, меняйте дополнительную фазу/задержку только после полной остановки процесса.
5. Найдите минимум измеряемого RMS, затем повторите для правого канала.
6. Не поднимайте strength, если результат нестабилен, появляются новые тоны или растёт RMS.

Значения `input latency + output latency` в статусе — сведения WASAPI, а не измеренная акустическая round-trip latency. Физическую задержку нужно измерить тестовым sweep/MLS в следующем этапе.

## Известные границы MVP 0.1

- нет калибровочного sweep и измерения secondary/reference path;
- нет ушного error-микрофона во время обычной работы;
- нет FxNLMS, онлайн-адаптации, FFT GUI и профилей;
- нет виртуального output/microphone драйвера;
- при разных sample rate входа и выхода запуск блокируется;
- коррекция clock drift между разными USB clock domains пока сводится к наблюдению xruns;
- console hotkey не является системной глобальной горячей клавишей.

Следующий технически обоснованный этап: автоматическое измерение round-trip latency и secondary path для каждой чашки, сохранение FIR-профиля и только затем FxNLMS с заморозкой при voice/impulse.
