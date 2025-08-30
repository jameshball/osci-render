num_lines = math.floor(slider_a * 10 + 5)
wave_size = slider_b * 3 + 2
wave_number = (slider_c * 10) + 0.1

t = t or 0
t = t + 5 / sample_rate

x = 2 * osci_saw_wave(phase * num_lines) - 1
y = 2 * math.floor(phase * num_lines / (2 * math.pi)) / num_lines - 1

sine_phase = wave_number / math.max(math.abs(x), 0)
waves = -wave_size * math.sin(sine_phase + t) / 10 + 0.25

return { x, y + waves, waves }