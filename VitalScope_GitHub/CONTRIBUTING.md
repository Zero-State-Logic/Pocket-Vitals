# Contributing to VitalScope

Thanks for your interest in making VitalScope better! This document explains how to contribute.

## Ways to contribute

- 🐛 **Report bugs** — open an issue with steps to reproduce
- 💡 **Suggest features** — open an issue describing the feature and why it's useful
- 📝 **Improve documentation** — fix typos, clarify instructions, add diagrams
- 🔧 **Add code** — implement features, fix bugs, optimize performance
- 📷 **Share your build** — photos and stories from your own VitalScope build

## Code contributions

### Setup

1. Fork the repository on GitHub
2. Clone your fork: `git clone https://github.com/YOUR_USERNAME/vitalscope.git`
3. Create a branch: `git checkout -b your-feature-name`

### Coding style

- Follow the existing code style (snake_case for functions, PascalCase for types)
- Comment any tricky logic — assume the reader hasn't seen this code before
- Keep functions short — ideally under 50 lines
- Avoid `delay()` in the main loop — use millis()-based timing
- Test on real hardware before submitting

### Pull request process

1. Make your changes
2. Test thoroughly on your hardware
3. Commit with a clear message: `git commit -m "Add: BLE companion app support"`
4. Push to your fork: `git push origin your-feature-name`
5. Open a pull request — describe what changed and why

## Feature priorities

These are areas where contributions would have the highest impact:

1. **SD card data logging** — historical tracking of vitals
2. **BLE streaming** — phone companion app integration
3. **Notch filter for ECG** — cleaner waveform by removing 50/60 Hz noise
4. **Custom PCB design** — KiCad files to replace veroboard
5. **Power optimization** — deep sleep between measurements
6. **Sensor variants** — alternate sensor support (BMP280, MAX30100, etc.)

## Code of conduct

Be kind. Be helpful. Assume good intent. We're all here to build cool things together.

## Questions?

Open an issue tagged `question` and someone will respond.
