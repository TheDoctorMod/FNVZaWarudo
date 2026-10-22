Available activation and resume audio:
	active.wav (No Voice activation)
	dio_activation.wav (DIO Brando activation)
	jotaro_activation.wav (Jotaro Kujo activation)
	resume.wav (shared resume cue)

The FOMOD installs exactly one activation selection. All runtime cues are mono,
16-bit PCM WAV at 44.1 kHz. Character-source files must be normalized with
tools\convert_audio.py before creating the final package.

The selected activation path is stored in Data\Config\TheWorldNVVoice.ini.
The shared resume path remains in Data\Config\TheWorldNV.ini. JohnnyGuitar
NVSE 5.17 or newer uses PlaySoundFromPath. Older versions use runtime Sound-
form clones with SetSoundSourceFile and PlaySound for compatible formats;
PlaySoundFile is intentionally avoided because it uses the music streamer.
