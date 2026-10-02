Scriptname BodycamPresets Native Hidden

; The MCM Save / Load buttons. Bodycam.dll watches the two counters in MCM/Settings/Bodycam.ini
; and does the file work. A counter, not a flag: MCM only writes a setting when its value changes.

Function Bump(string asSetting) global
	MCM.SetModSettingInt("Bodycam", asSetting, MCM.GetModSettingInt("Bodycam", asSetting) + 1)
EndFunction

Function Save() global
	Bump("iSaveTick:Presets")
EndFunction

Function Load() global
	Bump("iLoadTick:Presets")
EndFunction
