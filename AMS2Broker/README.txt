The AMS2Broker

Find all informations and latest releases here: 
https://github.com/dahas/AMS2Broker

(C) 2026 daHas (Martin Wolf)

-------------------------------------------------------------------------

QUICK USER GUIDE:

After you have clicked the button that starts the race, the launcher opens and runs the game. The first time this happens, Windows SmartScreen will likely pop up and block the launcher from running. If this happens click “More Information” and then “Run anyway”.

The Broker will automatically configurate the settings. Wait for it until the confirmation appears. The CHANGES ARE NOT VISIBLE at first! You'll see them when you start the session. 

If the confirmation appears before you've entered the 'Single Race', manually enter the 'Single Race' and press" INSERT (or the button you have configured in config.ini) to apply the patch.

-------------------------------------------------------------------------

CONFIGURATION:

There is a `config.ini` file in the Ams2Broker directory in which you can make certain changes. You can make the Broker completely invisible if you like. It is recommended that you wait to do this until everything has been working as expected for an extended period of time.

-------------------------------------------------------------------------

TROUBLESHOOTING:

The `config.ini` file also contains certain settings in case something doesn't work as expected.

> Game not found:
The path to Automobilista 2 can be entered there if the launcher cannot find the game automatically. 

> REST-API error:
You can use a different port if the API that triggers the patching is unresponsive. 

> Hotkeys already assigned
The hotkeys can be changed as well if their assignments conflict with other apps.

> Antivirus / Windows Defender False Positive:
Because Launcher.exe uses DLL injection to communicate with AMS2 and is not digitally signed, antivirus software (including Windows Defender) may flag or quarantine Launcher.exe or PatcherAPI.dll as a false positive.

If files disappear or fail to launch:
- Open Windows Security → Virus & threat protection → Protection history and restore the quarantined files.  
- Go to Manage settings → Exclusions and add the entire AMS2Broker folder as an exclusion.

-------------------------------------------------------------------------

KNOWN ISSUES:

- Mouse movements during the startup process can cause the broker to move in the wrong direction, resulting in the patch being applied at the wrong time and in the wrong place.

- Setting up Multiclass Races is not yet supported. You can set them up manually. 

- Livetrack patching not reliable. If the initial Livetrack setting is an important factor just enter the Session Settings and press save.

For all manual interventions, the Broker must be visible, “AutoStart” must be disabled, and the patching must have been confirmed.

