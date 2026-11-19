# USER GUIDE

After you have clicked the button that starts the race, the launcher opens and runs the game. The first time this happens, Windows SmartScreen will likely pop up and block the launcher from running. You must click “More Information” and then “Run anyway”.

The broker will automatically configure the settings. Wait for it until the confirmation appears. If the confirmation appears before you've entered the 'Single Race', manually enter the 'Single Race' and press" INSERT (or the button you have configured) to apply the patch.

### Troubleshooting:

There is a `config.ini` file in the Ams2Broker directory in which you can make certain changes if things doesn't work as expected. 

##### Game not found:
The path to Automobilista 2 can be entered there if the launcher cannot find the game automatically. 

##### REST-API error:
You can use a different port if the API that triggers the patching is unresponsive. 

##### Hotkeys already assigned
The hotkeys can be changed as well if their assignments conflict with other apps.

##### Antivirus / Windows Defender False Positive:
Because Launcher.exe uses DLL injection to communicate with AMS2 and is not digitally signed, antivirus software (including Windows Defender) may flag or quarantine Launcher.exe or PatcherAPI.dll as a false positive.

If files disappear or fail to launch:
- Open Windows Security → Virus & threat protection → Protection history and restore the quarantined files.  
- Go to Manage settings → Exclusions and add the entire AMS2Broker folder as an exclusion.  

