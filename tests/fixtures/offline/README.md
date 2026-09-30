# Offline speech fixture

`osr-speech-8s.wav` is an unchanged PCM excerpt (seconds 1 through 9) of
`OSR_us_000_0010_8k.wav`, mono 16-bit 8 kHz Harvard sentences.

Source: **Open Speech Repository**, sponsored by Telchemy Incorporated.

- Source file: https://www.voiptroubleshooter.com/open_speech/american/OSR_us_000_0010_8k.wav
- Source and conditions of use: https://www.voiptroubleshooter.com/open_speech/american.html
- Retrieved 2026-09-30.
- Original SHA256: `a4bf9becd046d7aedb6d05b6e12347a6294a44f74d263089c636fb0a2b1e6561`
- Excerpt SHA256: `6f7940c4be6adc40184c58ae1578ab1fec11d8da5983d59282c5095728e72154`

The source permits copying, downloading, broadcasting, modification and
incorporation in websites/test equipment for reasonable applications, with
identification as **Open Speech Repository**. This fixture retains that required
attribution and is distributed under those source conditions, rather than the
repository's code license.

Reproduce with Python's standard `wave` module: read original, seek to frame
8000, read 64000 frames, and write those frames with the same WAV parameters.
