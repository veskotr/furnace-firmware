Nextion Elements Documentation

**IMPORTANT FOR DEVELOPERS:** Always ask the user for component IDs before coding! Component IDs are assigned by Nextion Editor based on creation order and cannot be assumed.

---

Main Page

- preinit
	- Explanation: Hide error overlay, confirmation dialog. Hide manual control elements only on first entry (manualInit==0). Do NOT reset editedField here — postinit needs it to detect keyboard return.
	- Nextion code: Preinitialize Event
		vis errTxtHead,0
		vis errText,0
		vis errTxtCloseB,0
		vis confirmBdy,0
		vis confirmTxt,0
		vis confirmEnd,0
		vis confirmCancel,0
		if(manualInit.val==0)
		{
		  vis manualControlH,0
		  vis manCtrlBody,0
		  vis targetTempH,0
		  vis targetTemp,0
		  vis incTempB,0
		  vis decTempB,0
		  vis tDeltaTH,0
		  vis tDeltaT,0
		  vis incDeltaB,0
		  vis decDeltaB,0
		}
- progNameDisp
	- Explanation: TODO
	- Nextion code: TODO
- graphDisp (ID=6)
	- Explanation: Waveform graph. ESP32 renders projected program curve into channel 0.
	- Nextion code: TODO (ESP32 uses cle/add)
- timeElapsed
	- Explanation: TODO
	- Nextion code: TODO
- timeRamaining
	- Explanation: TODO
	- Nextion code: TODO
- currentTemp
	- Explanation: TODO
	- Nextion code: TODO
- currentKw
	- Explanation: Displays current power (kW) placeholder from ESP32.
	- Nextion code: None (ESP32 sets via currentKw.txt="value")
- addProg
	- Explanation: Start a new program (no overwrite). ESP32 handles page change.
	- Nextion code: Touch Release Event
		prints "add_prog",0
		printh 0d 0a
- progBwsr
	- Explanation: FileBrowser selection. Sends selected filename to ESP32 for loading and graph render.
	- Nextion code: Touch Release Event
		prints "prog_select:",0
		prints progBwsr.txt,0
		printh 0d 0a
- settingsPageB
	- Explanation: Navigate to Settings page. Nextion only emits an event; ESP32 handles page change.
	- Nextion code: Touch Release Event
		prints "nav:settings",0
		printh 0d 0a
- clockDisplay
	- Explanation: Displays current time from Nextion RTC. Updated by tm0 timer every second.
	- Nextion code: None (updated by timer)

- dateDisplay
	- Explanation: Displays current date from Nextion RTC. Updated by tm0 timer every second.
	- Nextion code: None (updated by timer)

- clockTimer (Timer, interval=1000ms, enabled=1)
	- Explanation: Updates clock and date display from Nextion's built-in RTC every second. Uses clockVar (va1) as text buffer.
	- Nextion code: Timer Event
		// Format time as HH:MM:SS
		// Hour
		if(rtc3<10)
		{
		  clockDisplay.txt="0"
		}else
		{
		  clockDisplay.txt=""
		}
		cov rtc3,clockVar.txt,0
		clockDisplay.txt+=clockVar.txt
		clockDisplay.txt+=":"
		// Minute
		if(rtc4<10)
		{
		  clockDisplay.txt+="0"
		}
		cov rtc4,clockVar.txt,0
		clockDisplay.txt+=clockVar.txt
		clockDisplay.txt+=":"
		// Second
		if(rtc5<10)
		{
		  clockDisplay.txt+="0"
		}
		cov rtc5,clockVar.txt,0
		clockDisplay.txt+=clockVar.txt
		// Format date as DD/MM/YYYY
		// Day
		if(rtc2<10)
		{
		  dateDisplay.txt="0"
		}else
		{
		  dateDisplay.txt=""
		}
		cov rtc2,clockVar.txt,0
		dateDisplay.txt+=clockVar.txt
		dateDisplay.txt+="/"
		// Month
		if(rtc1<10)
		{
		  dateDisplay.txt+="0"
		}
		cov rtc1,clockVar.txt,0
		dateDisplay.txt+=clockVar.txt
		dateDisplay.txt+="/"
		// Year
		cov rtc0,clockVar.txt,0
		dateDisplay.txt+=clockVar.txt

- clockVar (Variable va1, type=string, txt_maxl=10, vscope=global)
	- Explanation: Text buffer for clock/date timer formatting.
	- Nextion code: None

- startProgBtn
	- Explanation: TODO
	- Nextion code: Touch Release Event
		prints "prog_start",0
		printh 0d 0a

- pauseProgB
	- Explanation: TODO
	- Nextion code: Touch Release Event
		prints "prog_pause",0
		printh 0d 0a

- endProgBtn
	- Explanation: Request program end. Shows confirmation dialog; ESP32 handles display.
	- Nextion code: Touch Release Event
		prints "prog_stop",0
		printh 0d 0a

- editProgBtn
	- Explanation: Edit currently loaded program (allow overwrite).
	- Nextion code: Touch Release Event
		prints "edit_prog:",0
		prints progNameDisp.txt,0
		printh 0d 0a

- graphBorder
	- Explanation: Visual element to border the graph.
	- Nextion code: None

- machineState
	- Explanation: Displays current machine state like program running or loading file.
	- Nextion code: None

- confirmBdy
	- Explanation: Confirmation dialog background. Hidden on page entry. ESP32 shows on end program request.
	- Nextion code: None (ESP32 controls visibility)

- confirmTxt
	- Explanation: Confirmation dialog message (max 36 chars). ESP32 sets to "End program? Can't resume.".
	- Nextion code: None (ESP32 sets via confirmTxt.txt="...")

- confirmEnd
	- Explanation: Confirm end program button. Sends confirm_end to ESP32 which stops the profile and hides dialog.
	- Nextion code: Touch Release Event
		prints "confirm_end",0
		printh 0d 0a

- confirmCancel
	- Explanation: Cancel button. Hides confirmation dialog locally without ESP32 involvement.
	- Nextion code: Touch Release Event
		vis confirmBdy,0
		vis confirmTxt,0
		vis confirmEnd,0
		vis confirmCancel,0

- fanMode
	- Explanation: Toggle fan mode between "Max" and "Silent". Starting state is "Max". Pressing sends event to ESP32 which toggles mode and sends back text update. No local state tracking — ESP32 owns the state.
	- Nextion code: Touch Release Event
		prints "fan_mode_toggle",0
		printh 0d 0a

- opTime
	- Explanation: Displays total operational time of the machine to date. Incremented by ESP32 each time timeElapsed is updated during a running program. ESP32 persists this value in NVS.
	- Nextion code: None (ESP32 sets via opTime.txt="value")

- startManual
	- Explanation: Start/stop manual heating mode. On press: if manual mode is inactive, shows manual control elements (manualControlH, manCtrlBody, targetTemp, targetTempH, incTempB, decTempB, tDeltaTH, tDeltaT, incDeltaB, decDeltaB) and sends start event. If manual mode is active, hides elements and sends stop event. ESP32 uses same heater start mechanism as prog_start. Blocked while a program is running (router guard).
	- Nextion code: Touch Release Event
		prints "manual_toggle",0
		printh 0d 0a

- manualControlH
	- Explanation: Title label for manual control section. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: None (ESP32 controls visibility)

- manCtrlBody
	- Explanation: Background UI element for manual control section. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: None (ESP32 controls visibility)

- targetTempH
	- Explanation: Title label for target temperature field. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: None (ESP32 controls visibility)

- targetTemp
	- Explanation: Displays and allows editing of manual mode target temperature (°C). Default value "20" (cfg_Tmin). Editable via keyboard (has .key set) or via incTempB/decTempB buttons. On keyboard confirm, Postinitialize detects editedField.val==1 and sends value to ESP32. ESP32 clamps to [cfg_Tmin, cfg_Tmax] and updates display. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: Touch Press Event
		editedField.val=1

- incTempB
	- Explanation: Increase manual target temperature by 5°C. Sends event to ESP32 which validates new value (must not exceed cfg_Tmax), updates targetTemp.txt, and adjusts heater target if running. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: Touch Release Event
		prints "manual_temp_inc",0
		printh 0d 0a

- decTempB
	- Explanation: Decrease manual target temperature by 5°C. Sends event to ESP32 which validates new value (must not go below cfg_Tmin=20°C — shows error if attempted), updates targetTemp.txt, and adjusts heater target if running. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: Touch Release Event
		prints "manual_temp_dec",0
		printh 0d 0a

- tDeltaTH
	- Explanation: Title label for temperature delta field. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: None (ESP32 controls visibility)

- tDeltaT
	- Explanation: Displays and allows editing of manual mode temperature delta (°C/min). Default value "1.0". Editable via keyboard (has .key set) or via incDeltaB/decDeltaB buttons. On keyboard confirm, Postinitialize detects editedField.val==2 and sends value to ESP32. ESP32 clamps to [0.1, cfg_dTmax] and updates display. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: Touch Press Event
		editedField.val=2

- incDeltaB
	- Explanation: Increase manual delta-T by 0.1°C/min. Sends event to ESP32 which validates (must not exceed cfg_dTmax), updates tDeltaT.txt, and adjusts heater behavior if running. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: Touch Release Event
		prints "manual_delta_inc",0
		printh 0d 0a

- decDeltaB
	- Explanation: Decrease manual delta-T by 0.1°C/min. Sends event to ESP32 which validates (must not go below 0.1°C/min — shows error if attempted), updates tDeltaT.txt, and adjusts heater behavior if running. Hidden on page entry; shown by ESP32 when manual mode starts.
	- Nextion code: Touch Release Event
		prints "manual_delta_dec",0
		printh 0d 0a

- manualInit (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks whether manual mode controls are active. 0=hidden (first entry / manual stopped), 1=shown (manual running). ESP32 sets to 1 in show_manual_controls(), 0 in nextion_hide_manual_controls(). Preinit uses this to skip hiding manual elements when returning from keyboard popup. MUST be global scope to survive keyboard page navigation.
	- Nextion code: None (ESP32 sets via manualInit.val=0/1)

- editedField (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks which field was edited via keyboard popup. 0=none, 1=targetTemp, 2=tDeltaT. Set in field's Touch Press Event before keyboard opens. Checked and reset in Postinitialize. MUST be global scope to survive keyboard page navigation. NOT reset in preinit — postinit needs it to detect keyboard return vs normal page entry.
	- Nextion code: None (set by targetTemp/tDeltaT Touch Press, checked in postinit)

- postinit
	- Explanation: Fires after returning from keyboard page. Checks editedField and sends the edited value to ESP32. Resets flag to 0 after sending. Cancel (no value written) is harmless — the field still holds its previous value which ESP32 will clamp/accept.
	- Nextion code: Postinitialize Event
		if(editedField.val==1)
		{
		  prints "manual_temp_set:",0
		  prints targetTemp.txt,0
		  printh 0d 0a
		  editedField.val=0
		}else if(editedField.val==2)
		{
		  prints "manual_delta_set:",0
		  prints tDeltaT.txt,0
		  printh 0d 0a
		  editedField.val=0
		}

Manual Mode Lifecycle:
- Main page preinit hides manual elements ONLY when manualInit.val==0 (first entry / manual not active)
- ESP32 show_manual_controls() sets manualInit.val=1, nextion_hide_manual_controls() sets manualInit.val=0
- When keyboard popup returns, preinit fires but skips hiding manual controls (manualInit.val==1)
- ESP32 shows them on "manual_toggle" (start) and hides them on "manual_toggle" (stop) or when a program stops (nextion_event_handle_profile_stopped)
- Default values on show: targetTemp.txt="20", tDeltaT.txt="1.0"
- ESP32 router: manual_toggle, manual_temp_inc, manual_temp_dec, manual_delta_inc, manual_delta_dec, manual_temp_set:<value>, manual_delta_set:<value> are always-allowed commands (like prog_start)
- Validation: temp clamped to [cfg_Tmin=20, cfg_Tmax=170], delta clamped to [0.1, cfg_dTmax]

Settings Page

ESP32 sends cfg_* (hardware limits) values on page init.
User config overrides are removed.
Time/date fields read from Nextion RTC and are only saved if user edits them (tracked by timeDirty variable).

- timeDirty (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if user edited time fields (hour/min/sec). 0=not edited, 1=edited. MUST be global scope.
	- Nextion code: None (set by hour/min/sec touch events, sent with save, reset on preinit)

- dateDirty (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if user edited date fields (day/month/year). 0=not edited, 1=edited. MUST be global scope.
	- Nextion code: None (set by day/month/year touch events, sent with save, reset on preinit)

- timeInit (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if time fields were populated this page visit. MUST be global scope to persist through keyboard popup.
	- Nextion code: None (checked/set in preinit)

- settingsVar (Variable, type=string, txt_maxl=10, vscope=local)
	- Explanation: Text buffer for settings page operations (converting timeDirty to text for send).
	- Nextion code: None

- ambientDirty (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if user edited the ambient temperature field. 0=not edited, 1=edited. MUST be global scope.
	- Nextion code: None (set by ambientTemp touch event, sent with save, reset on preinit)

- cooldownDirty (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if user edited the cooldown rate field. 0=not edited, 1=edited. MUST be global scope.
	- Nextion code: None (set by cooldownRate touch event, sent with save, reset on preinit)

- ambientTempH
	- Explanation: Heading label for ambient temperature field. Default text: "Set ambient temp". ESP32 updates to "Set ambient temp (current: Y)" on page init, where Y is the live sensor reading.
	- Nextion code: None (ESP32 sets via ambientTempH.txt="...")

- ambientTemp
	- Explanation: User-editable ambient/room temperature (°C). Populated by ESP32 on page init with NVS-backed preference value (default 23). Used as starting temperature for program save-time validation and graph rendering.
	- Nextion code: Touch Press Event
		ambientDirty.val=1

- cooldownH
	- Explanation: Heading label for cooldown rate field. Default text: "Cooling rate (°C/min)".
	- Nextion code: None (static label)

- cooldownRate
	- Explanation: User-editable cooldown rate (°C/min). Populated by ESP32 on page init with NVS-backed value (default from Kconfig: 1.0). Editable via keyboard (has .key set). Saved when user presses Save button. ESP32 clamps to [0.1, cfg_dTmax] and saves to NVS. Used for prediction graph cooldown phase and runtime cooldown calculations.
	- Nextion code: Touch Press Event
		cooldownDirty.val=1

- preinit
	- Explanation: Hide error overlay and optional connectivity elements. Only populate time fields once per page visit (check timeInit flag). ESP32 selectively shows websiteQr/wirelessCfgB/bluetoothCfgB via handle_settings_init() based on NEXTION_HAS_WIFI and NEXTION_HAS_BLUETOOTH Kconfig options.
	- Nextion code: Preinitialize Event
		vis errTxtHead,0
		vis errText,0
		vis errTxtCloseB,0
		vis websiteQrHead,0
		vis websiteQr,0
		vis wirelessCfgB,0
		vis bluetoothCfgB,0
		vis confirmBdy,0
		vis confirmTxt,0
		vis confirmFactoryB,0
		vis confirmCancel,0
		// Only populate time fields on first preinit (page entry)
		if(timeInit.val==0)
		{
		  timeDirty.val=0
		  dateDirty.val=0
		  ambientDirty.val=0
		  cooldownDirty.val=0
		  cov rtc3,hourInput.txt,0
		  cov rtc4,minutesInput.txt,0
		  cov rtc5,secondsInput.txt,0
		  cov rtc2,dayInput.txt,0
		  cov rtc1,monthInput.txt,0
		  cov rtc0,yearInput.txt,0
		  timeInit.val=1
		}

- postinit
	- Explanation: No code needed — all settings are sent via save button.
	- Nextion code: None

- pageExit (use Page Exit Event or backBtn)
	- Explanation: Reset timeInit flag when leaving page so it re-populates on next visit.
	- Note: Add timeInit.val=0 to backBtn Touch Release Event before prints

- backBtn
	- Explanation: Navigate back to Main page. Resets timeInit flag for next visit.
	- Nextion code: Touch Release Event
		timeInit.val=0
		prints "nav:main",0
		printh 0d 0a

- tempScaleBtn
	- Explanation: Toggle temperature scale (C/F). Local HMI logic only.
	- Nextion code: TODO (local toggle, no ESP32 event)

- saveSpecsB
	- Explanation: Send time/date/ambient/cooldown values to ESP32. Includes timeDirty, dateDirty, ambientDirty, and cooldownDirty flags as ASCII. Total 12 comma-separated tokens.
	- Nextion code: Touch Release Event
		prints "save_settings:",0
		cov timeDirty.val,settingsVar.txt,0
		prints settingsVar.txt,0
		printh 2c
		cov dateDirty.val,settingsVar.txt,0
		prints settingsVar.txt,0
		printh 2c
		prints hourInput.txt,0
		printh 2c
		prints minutesInput.txt,0
		printh 2c
		prints secondsInput.txt,0
		printh 2c
		prints dayInput.txt,0
		printh 2c
		prints monthInput.txt,0
		printh 2c
		prints yearInput.txt,0
		printh 2c
		cov ambientDirty.val,settingsVar.txt,0
		prints settingsVar.txt,0
		printh 2c
		prints ambientTemp.txt,0
		printh 2c
		cov cooldownDirty.val,settingsVar.txt,0
		prints settingsVar.txt,0
		printh 2c
		prints cooldownRate.txt,0
		printh 0d 0a

- cfg_t
	- Explanation: Read-only. Displays max operational time limit (minutes). Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_t.txt="value")

- cfg_Tmax
	- Explanation: Read-only. Displays max temperature limit (°C). Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_Tmax.txt="value")

- cfg_Tmin
	- Explanation: Read-only. Displays min temperature limit (°C). Not currently sent by ESP32 — reserved for future use. Default 20.
	- Nextion code: None (display-only, not populated at runtime)

- cfg_dt
	- Explanation: Read-only. Displays min sensor read frequency (seconds). Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_dt.txt="value")

- cfg_dTmax
	- Explanation: Read-only. Displays max delta-T per minute. Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_dTmax.txt="value")

- cfg_Power
	- Explanation: Read-only. Displays max power the device heating element can output in KW. Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_Power.txt="value")

- hourInput
	- Explanation: RTC hour input (00-23). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		timeDirty.val=1

- minutesInput
	- Explanation: RTC minutes input (00-59). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		timeDirty.val=1

- secondsInput
	- Explanation: RTC seconds input (00-59). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		timeDirty.val=1

- dayInput
	- Explanation: RTC day input (01-31). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		dateDirty.val=1

- monthInput
	- Explanation: RTC month input (01-12). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		dateDirty.val=1

- yearInput
	- Explanation: RTC year input (2000-2099). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		dateDirty.val=1

- websiteQrHead
	- Explanation: QR code heading. Hidden on preinit; ESP32 shows via vis websiteQr,1 when CONFIG_NEXTION_HAS_WIFI is enabled.
	- Nextion code: None (ESP32 controls visibility)
- websiteQr
	- Explanation: QR code for website. Hidden on preinit; ESP32 shows via vis websiteQr,1 when CONFIG_NEXTION_HAS_WIFI is enabled.
	- Nextion code: None (ESP32 controls visibility)

- wirelessCfgB
	- Explanation: WiFi configuration button. Hidden on preinit; ESP32 shows via vis wirelessCfgB,1 when CONFIG_NEXTION_HAS_WIFI is enabled.
	- Nextion code: TODO (future feature, button visible only when WiFi is available)

- bluetoothCfgB
	- Explanation: Bluetooth configuration button. Hidden on preinit; ESP32 shows via vis bluetoothCfgB,1 when CONFIG_NEXTION_HAS_BLUETOOTH is enabled.
	- Nextion code: TODO (future feature, button visible only when Bluetooth is available)

- restartB
	- Explanation: Restart ESP32 and Nextion display. ESP32 sends Nextion `rest` command then calls esp_restart(). Blocked while a program is running (router guard).
	- Nextion code: Touch Release Event
		prints "restart",0
		printh 0d 0a

- factoryResetB
	- Explanation: Open factory reset confirmation dialog. Does NOT directly reset — ESP32 shows confirmBdy/confirmTxt/confirmFactoryB/confirmCancel. Blocked while a program is running.
	- Nextion code: Touch Release Event
		prints "factory_reset",0
		printh 0d 0a

- confirmReset
	- Explanation: Confirm factory reset button. ESP32 deletes all tracked programs from SD, resets Nextion, and reboots ESP32.
	- Nextion code: Touch Release Event
		prints "factory_reset_confirm",0
		printh 0d 0a

- confirmCancel
	- Explanation: Cancel factory reset. Hides confirmation dialog locally without ESP32 involvement.
	- Nextion code: Touch Release Event
		vis confirmBdy,0
		vis confirmTxt,0
		vis confirmReset,0
		vis confirmCancel,0

Programs Page

Method 2 Architecture:
- ESP32 holds program draft in memory (15 stages total)
- Only 5 stage fields exist on HMI (bStg1-5, t1-5, etc.)
- Page buttons send current page data to ESP32, which stores it and sends back new page data
- Save/Graph buttons send current page data; ESP32 merges with draft for full program

- dirty (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if user edited any field on the programs page. 0=not edited, 1=edited. MUST be global scope to survive keyboard popup. Set by Touch Press events on all editable fields. ESP32 resets to 0 on page entry (handle_add_prog / handle_edit_prog). backToMain sends this value to ESP32 — if 1, ESP32 opens confirm dialog with "Exit" option.
	- Nextion code: None (set by field touch events, reset by ESP32)

- progEditField (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks which field was edited via keyboard popup on Programs page. Encoding: type*10+row (11-15=time, 21-25=targetTemp, 31-35=tempDelta, 41-45=tDelta). Set in field's Touch Press Event before keyboard opens. Checked and reset in Postinitialize. MUST be global scope to survive keyboard page navigation.
	- Nextion code: None (set by field Touch Press, checked in postinit)

- progVar (Variable, type=string, txt_maxl=4, vscope=local)
	- Explanation: Small text buffer for converting progEditField integer to text in Postinitialize prints command.
	- Nextion code: None

- preinit
	- Explanation: Hide error overlay, graph, and confirmation dialog on page entry.
	- Nextion code: Preinitialize Event
		vis errTxtHead,0
		vis errText,0
		vis errTxtCloseB,0
		vis graphDisp,0
		vis confirmBdy,0
		vis confirmTxt,0
		vis confirmDelete,0
		vis confirmCancel,0

- postinit
	- Explanation: Fires after returning from keyboard page. Checks progEditField and sends the edited field code + value to ESP32 for validation. Reads the entered value from keybdB.kbVal.txt (set by keyboard OK handler). Resets flag to 0 after sending.
	- Nextion code: Postinitialize Event
		if(progEditField.val>0)
		{
		  prints "prog_field:",0
		  cov progEditField.val,progVar.txt,0
		  prints progVar.txt,0
		  printh 2c
		  prints keybdB.kbVal.txt,0
		  printh 0d 0a
		  progEditField.val=0
		}

- backToMain
	- Explanation: Navigate back to Main page. Sends dirty flag to ESP32 — if dirty, ESP32 opens confirm dialog; if not dirty, ESP32 navigates directly.
	- Nextion code: Touch Release Event
		if(dirty.val==1)
		{
		  prints "prog_back:1",0
		  printh 0d 0a
		}else
		{
		  prints "prog_back:0",0
		  printh 0d 0a
		}

- bPrevP
	- Explanation: Navigate to previous page. Sends current page data to ESP32 for sync.
	- Nextion code: Touch Release Event
		prints "prog_page_data:prev,",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- pageNum
	- Explanation: Displays current page (1-3). Set by ESP32.
	- Nextion code: None (ESP32 sets via pageNum.txt="X")

- bNextP
	- Explanation: Navigate to next page. Sends current page data to ESP32 for sync.
	- Nextion code: Touch Release Event
		prints "prog_page_data:next,",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- progNameInput
	- Explanation: Program name text input field. Sets dirty flag on touch.
	- Nextion code: Touch Press Event
		dirty.val=1

- tempScaleBtn
	- Explanation: TODO
	- Nextion code: TODO


- showGraph
	- Explanation: Toggle graph preview. First click sends current page data to ESP32 which renders graph and sets button text to "Hide Graph". Second click hides graph and resets text to "Show Graph". Default button text: "Show Graph".
	- Nextion code: Touch Release Event
		prints "show_graph:",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- saveProgBtn
	- Explanation: Validate and save program. Sends current page data; ESP32 merges with draft and saves.
	- Nextion code: Touch Release Event
		prints "save_prog:",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- bStg1-5
	- Explanation: Stage number labels. ESP32 updates these when changing pages (1-5, 6-10, 11-15).
	- Nextion code: None (ESP32 sets via bStgX.txt="Y")

- t1-5
	- Explanation: Time input fields for current page's 5 stages. Sets dirty flag on touch.
	- Nextion code: Touch Press Event (each field)
		dirty.val=1
		progEditField.val=1X    // where X is the field number: t1=11, t2=12, t3=13, t4=14, t5=15

- targetTMax1-5
	- Explanation: Target temperature fields for current page's 5 stages. Sets dirty flag on touch.
	- Nextion code: Touch Press Event (each field)
		dirty.val=1
		progEditField.val=2X    // targetTMax1=21, targetTMax2=22, ..., targetTMax5=25

- tDelta1-5
	- Explanation: Time delta fields for current page's 5 stages. Sets dirty flag on touch.
	- Nextion code: Touch Press Event (each field)
		dirty.val=1
		progEditField.val=4X    // tDelta1=41, tDelta2=42, ..., tDelta5=45

- tempDelta1-5
	- Explanation: Temperature delta fields for current page's 5 stages. Sets dirty flag on touch.
	- Nextion code: Touch Press Event (each field)
		dirty.val=1
		progEditField.val=3X    // tempDelta1=31, tempDelta2=32, ..., tempDelta5=35

- autofillBtn
	- Explanation: Calculate missing time or delta_T fields. Requires target_temp + (time OR delta_T).
	- Formulas: t = T / delta_T, or delta_T = T / t, where T = target_T - current_T
	- Nextion code: Touch Release Event
		prints "autofill:",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- deleteBtn
	- Explanation: Request program deletion or field clear. In edit mode (s_original_program_name set): checks name match and opens delete confirmation. In new program mode: opens clear confirmation. ESP32 sets button text to "Delete" or "Clear" on page entry depending on mode.
	- Nextion code: Touch Release Event
		prints "delete_prog:",0
		prints progNameInput.txt,0
		printh 0d 0a

- Confirm dialog
- confirmBdy
	- Explanation: Confirmation dialog background. Hidden on page entry. ESP32 shows for delete, clear, or dirty-exit actions.
	- Nextion code: None (ESP32 controls visibility)

- confirmTxt
	- Explanation: Confirmation dialog message text (max 40 chars). ESP32 sets based on context: "Delete \"name\"?" for delete, "Clear all program data?" for clear, "Unsaved changes will be lost. Exit?" for dirty back.
	- Nextion code: None (ESP32 sets via confirmTxt.txt="...")

- confirmDelete
	- Explanation: Confirm action button. Context-dependent: sends confirm_delete to ESP32 which dispatches based on dialog context — delete program (edit mode), clear all fields (new program mode), or exit without saving (dirty back). ESP32 sets button text to "Delete", "Clear", or "Exit" depending on context.
	- Nextion code: Touch Release Event
		prints "confirm_delete",0
		printh 0d 0a

- confirmReset
	- Explanation: Confirm reset button. Sends event to ESP32 which resets data and hides dialog.
	- Nextion code: Touch Release Event
		prints "confirm_reset",0
		printh 0d 0a

- confirmEnd
	- Explanation: Confirm end program button. Sends event to ESP32 which ends program execution and hides dialog.
	- Nextion code: Touch Release Event
		prints "confirm_end",0
		printh 0d 0a


- confirmCancel
	- Explanation: Cancel deletion button. Hides confirmation dialog locally without ESP32 involvement.
	- Nextion code: Touch Release Event
		vis confirmBdy,0
		vis confirmTxt,0
		vis confirmDelete,0
		vis confirmCancel,0

- graphDisp (ID=50)
	- Explanation: Waveform graph for program preview. Hidden on entry; ESP32 shows and renders.
	- Nextion code: None (ESP32 uses vis/cle/add)

- programBuffer
	- Explanation: Serialized program draft buffer (text variable). ESP32 syncs this when editing/switching pages.
	- Nextion code: None

Common 

- Error text display
- errText
	- Explanation: Error body text. Text component shown/hidden by ESP32; content set by ESP32.
	- Nextion code: TODO
- errTxtHead
	- Explanation: Error title/header text. Text component shown/hidden by ESP32.
	- Nextion code: TODO
- errTxtCloseB
	- Explanation: Close button for error overlay. Nextion emits event; ESP32 hides and clears error text.
	- Nextion code: Touch Release Event
		prints "err:close",0
		printh 0d 0a
- Other
- fs0 (filestreamer tool)
	- Explanation: FileStream component used to save program files on Nextion SD. ESP32 calls fs0.open("sd0/<name>.prg"), fs0.val=0, fs0.write(va0.txt,0,<len>), fs0.close().
	- Nextion code: TODO (ESP32 drives methods)
- va0 (variable)
	- Explanation: Global text buffer used as temporary storage for program data before FileStream write. Set .txt_maxl to 4096 (matches PROGRAM_FILE_SIZE).
	- Nextion code: TODO (ESP32 sets va0.txt)

Keyboard Page (numberinc / keyboardB)

This is a reusable input page invoked by any component that has `.key` set to this page. Before navigating here, the caller sets two global variables:
- `loadpageid.val` = the caller page's page id
- `loadcmpid.val` = the caller component's .id

The page auto-detects the caller component type and behaves accordingly:
- Type 54 (Number component): converts `.val` to text for editing, writes back as integer on OK.
- Type 59 (Xfloat component): decomposes `.val` using `.vvs1` decimal places for editing, recomposes on OK.
- Type 116 (Text component): copies `.txt` for editing, writes back as text on OK. Respects `.pw` (password mask).

On OK, the value is written directly back to `p[loadpageid.val].b[loadcmpid.val]` and then navigates back to the caller page via `page loadpageid.val`. This triggers the caller page's Postinitialize Event — which is where we can send the edited value to ESP32.

- loadpageid (Variable, type=int, vscope=global)
	- Explanation: Stores the page id of the calling page. Must be set before navigating to keyboard page. MUST be global scope.
	- Nextion code: None (set by caller before page change)

- loadcmpid (Variable, type=int, vscope=global)
	- Explanation: Stores the component .id of the calling component. Must be set before navigating to keyboard page. MUST be global scope.
	- Nextion code: None (set by caller before page change)

- input (Text component, vscope=local)
	- Explanation: Internal text buffer holding the user's current input string. Populated from caller component on preinit. Written back to caller on OK.
	- Nextion code: None (managed by page logic)

- show (Text component, vscope=local)
	- Explanation: Visible display of the current input. Mirrors `input.txt`. Supports `.pw` password masking for type 116 text components.
	- Nextion code: None (updated by page logic)

- inputlenth (Variable, type=int, vscope=local)
	- Explanation: Max allowed input length. Set from caller's `.txt_maxl` (text/number) or hardcoded 24 (xfloat). Prevents input from exceeding field capacity.
	- Nextion code: None (set in preinit)

- temp (Variable, type=int, vscope=local)
	- Explanation: Temporary integer variable used in xfloat decomposition/recomposition math.
	- Nextion code: None (used internally)

- temp2 (Variable, type=int, vscope=local)
	- Explanation: Second temporary integer variable used in xfloat math and string length operations.
	- Nextion code: None (used internally)

- tempstr (Variable, type=string, vscope=local)
	- Explanation: Temporary string buffer used for intermediate conversions (covx/cov/substr results).
	- Nextion code: None (used internally)

- kbVal (Variable, type=string, txt_maxl=10, vscope=global)
	- Explanation: Stores the last confirmed input value as text. Set in b210 (OK) before navigating back to caller page. Allows caller pages to read the entered value via keybdB.kbVal.txt without needing the complex p[]/b[] expression. MUST be global scope.
	- Nextion code: None (set by b210 OK handler)

- preinit
	- Explanation: Detects caller component type and populates input field accordingly. Handles Number (type 54), Xfloat (type 59), and Text (type 116) components. For Xfloat, decomposes the integer `.val` into a decimal string using `.vvs1` decimal places.
	- Nextion code: Preinitialize Event
		//Assign loadpageid.val and loadcmpid.val before call this page.
		//loadpageid.val is the caller page id, loadcmpid.val is the caller component id.
		if(p[loadpageid.val].b[loadcmpid.val].type==54)
		{
		  covx p[loadpageid.val].b[loadcmpid.val].val,input.txt,0,0
		  inputlenth.val=input.txt_maxl
		}else if(p[loadpageid.val].b[loadcmpid.val].type==59)
		{
		  inputlenth.val=p[loadpageid.val].b[loadcmpid.val].val
		  if(inputlenth.val<0)
		  {
		    inputlenth.val*=-1
		    input.txt="-"
		  }else
		  {
		    input.txt=""
		  }
		  temp.val=1
		  for(temp2.val=0;temp2.val<p[loadpageid.val].b[loadcmpid.val].vvs1;temp2.val++)
		  {
		    temp.val*=10
		  }
		  temp2.val=inputlenth.val/temp.val
		  cov temp2.val,tempstr.txt,0
		  input.txt+=tempstr.txt+"."
		  temp2.val=temp2.val*temp.val-inputlenth.val
		  if(temp2.val<0)
		  {
		    temp2.val*=-1
		  }
		  covx temp2.val,tempstr.txt,p[loadpageid.val].b[loadcmpid.val].vvs1,0
		  input.txt+=tempstr.txt
		  inputlenth.val=24
		}else
		{
		  input.txt=p[loadpageid.val].b[loadcmpid.val].txt
		  inputlenth.val=p[loadpageid.val].b[loadcmpid.val].txt_maxl
		  if(p[loadpageid.val].b[loadcmpid.val].type==116)
		  {
		    show.pw=p[loadpageid.val].b[loadcmpid.val].pw
		  }
		}
		show.txt=input.txt

- b249 (hide/show input toggle)
	- Explanation: Toggles password masking on the display field. Only visually meaningful for Text (type 116) components with .pw support.
	- Nextion code: Touch Release Event
		if(show.pw==0)
		{
		  show.pw=1
		}else
		{
		  show.pw=0
		}

- b251 (close page / cancel)
	- Explanation: Closes keyboard page without saving. Returns to caller page. No value is written back.
	- Nextion code: Touch Release Event
		page loadpageid.val

- b200 (delete / backspace)
	- Explanation: Removes the last character from the input string.
	- Nextion code: Touch Release Event
		input.txt-=1
		show.txt=input.txt

- b210 (OK / confirm)
	- Explanation: Writes the edited value back to the caller component and returns to caller page. Handles Number (type 54) by converting text to .val, Xfloat (type 59) by recomposing decimal string into integer .val with decimal place scaling, and Text by direct .txt assignment.
	- Nextion code: Touch Release Event
		//Assign loadpageid.val and loadcmpid.val before call this page.
		//loadpageid.val is the caller page id, loadcmpid.val is the caller component id.
		if(p[loadpageid.val].b[loadcmpid.val].type==54)
		{
		  covx input.txt,p[loadpageid.val].b[loadcmpid.val].val,0,0
		}else if(p[loadpageid.val].b[loadcmpid.val].type==59)
		{
		  covx input.txt,temp.val,0,0
		  if(temp.val<0)
		  {
		    temp.val*=-1
		  }
		  for(temp2.val=0;temp2.val<p[loadpageid.val].b[loadcmpid.val].vvs1;temp2.val++)
		  {
		    temp.val*=10
		  }
		  p[loadpageid.val].b[loadcmpid.val].val=temp.val
		  strlen input.txt,temp.val
		  temp.val--
		  while(temp.val>=0)
		  {
		    substr input.txt,tempstr.txt,temp.val,1
		    if(tempstr.txt==".")
		    {
		      substr input.txt,tempstr.txt,temp.val+1,p[loadpageid.val].b[loadcmpid.val].vvs1
		      covx tempstr.txt,temp2.val,0,0
		      strlen tempstr.txt,temp.val
		      while(temp.val<p[loadpageid.val].b[loadcmpid.val].vvs1)
		      {
		        temp2.val*=10
		        temp.val++
		      }
		      p[loadpageid.val].b[loadcmpid.val].val+=temp2.val
		      temp.val=-1
		    }
		    temp.val--
		  }
		  substr input.txt,tempstr.txt,0,1
		  if(tempstr.txt=="-")
		  {
		    p[loadpageid.val].b[loadcmpid.val].val*=-1
		  }
		}else
		{
		  p[loadpageid.val].b[loadcmpid.val].txt=input.txt
		}
		kbVal.txt=input.txt
		page loadpageid.val

- b0 through b10, b99 (input digit/character buttons)
	- Explanation: Each button appends its own label text to the input string (b0 appends "0", b1 appends "1", ..., b9 appends "9", b10 appends "0", b99 appends "."). Checks byte length against inputlenth.val to prevent overflow.
	- Nextion code: Touch Release Event (shown for b1, pattern identical for all — replace b1.txt with bN.txt)
		btlen input.txt,temp.val
		if(temp.val<inputlenth.val)
		{
		  input.txt+=b1.txt
		  show.txt=input.txt
		}

Integration Notes (for ESP32 developers):

The keyboard page writes the value back to the caller component and then does `page loadpageid.val`, which reloads the caller page. This means:
1. The caller page's **Postinitialize Event** fires after every keyboard confirm.
2. The edited value is already written into the component's `.txt` (or `.val`) by the time Postinitialize runs.
3. To send the edited value to ESP32 instantly on confirm, add code in the caller page's Postinitialize (or use a global flag variable set in Touch Press to know which field was edited).
4. Closing/canceling (b251) also triggers Postinitialize but does NOT write any value back — use a flag to distinguish.