# Архитектура MVP 0.1

```text
WASAPI capture event (MMCSS)
  -> channel downmix
  -> lock-free SPSC ring
  -> WASAPI render event (MMCSS)
       -> DC blocker
       -> 2nd-order HPF
       -> 2nd-order LPF
       -> manual delay/phase L/R
       -> polarity inversion
       -> smooth strength + channel gain
       -> impulse ducking
       -> soft limiter + -12 dBFS ceiling
       -> stereo WASAPI Shared stream
       -> Windows Audio Engine mixes program audio
```

В callback-подобных capture/render циклах нет allocation, файлового I/O, GUI и блокирующих mutex. Настройки и все delay/ring buffers создаются до запуска потоков.

## Границы ответственности

- `DeviceManager` перечисляет только активные Windows endpoints.
- `WasapiEngine` владеет COM audio clients, event handles и двумя realtime-потоками.
- `SpscRingBuffer` передаёт mono reference из clock domain входа в clock domain выхода.
- `ManualAncProcessor` — статический, ограниченный и детерминированный DSP-контур.
- Windows Audio Engine выполняет смешивание с обычным звуком в Shared mode.

При ошибке capture микрофон помечается offline и Emergency Stop фиксируется. При ошибке render фиксируется Emergency Stop и engine завершает работу. Обычный звук Windows не проходит через приложение и поэтому продолжает работать независимо.

## Почему пока нет FxNLMS

Без измеренной модели secondary path `S_L/S_R` и error-сигнала возле уха обновление коэффициентов FxNLMS было бы математически некорректным и потенциально опасным. MVP проверяет фазу, реальную задержку и достижимое ослабление на одном тоне; адаптивный фильтр должен появиться после калибровочного тракта.
