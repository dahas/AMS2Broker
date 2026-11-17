# AMS2Broker

## What is it?
The AMS2Broker is a tool that enables the automatic configuration of a single race in AUTOMOBILISTA2 (AMS2) from any external application.

## Who needs it and why?
It is intended for modders and application developers who want to create their own championships, special scenarios, and/or complex careers based on AUTOMOBILISTA2.

## How does it work?
The AMS2Broker consists of two parts: a launcher (Launcher.exe) and a DLL (PatcherAPI.dll). The launcher controls the game and injects the DLL. The DLL then acts as a REST API. It receives the configuration and patches the game with the corresponding settings at the click of a button.

## What can’t the AMS2Broker do?
In the current version, it cannot automatically configure multi-class races. If multi-class races are required, the user must configure these settings manually - AFTER he applied the patch!

## What should I know?
- AMS2 must be launched in “borderless” mode.
- For single-player mode only.
- Settings are patched in the background and the changes are not directly visible on the screen.
- After patching settings can be manually overwritten by the user.
- It is the responsibility of the developer that implements the tool to ensure that only vehicles and tracks owned by the user are loaded.
- AMS2 must be started and stopped separately for each race.

## Is it safe to use the AMS2Broker?
I developed this tool in my spare time with no financial interest. That’s why I didn’t spend any money on an expensive code-signing certificate. Because of the missing certificate, Windows blocks the tool from running. To run the tool anyway, you’ll once need to click “More info” and then “Run anyway.” 

If that seems too risky: the source code is open. You can examine it and compile the launcher and the DLL yourself. Make sure the names remain exactly the same.

Additionally I’ve used a Random Access Token to prevent third-party software from silently communicating with the REST API. This also prevents malware from being injected from the outside.

So in my opinion, it’s as safe as it can be.

## Which programming languages are supported?
All of them. The race weekend is configured directly through the launcher. Therefore, the application that uses the broker can be developed in any language and using any technology.

## Can users create their own races and championships?
Yes, they can, but this requires the developer to implement a feature that loads the additional files into their app.

## In summary:
The AMS2Broker is a tool for automatically configuring a single race. It offers maximum security and convenience for the user and maximum flexibility for the developer.



# DEVELOPER GUIDE

### Installation
Download the ZIP archive from here and extract it directly into your app's directory.

All races are configured using JSON templates. The template files must be stored in the “Path\To\Ams2Broker\Templates\” directory.

The Templates folder contains 3 examples:
full-example.json
quick-race-example.json
placeholders-example.json

To familiarize yourself with the complete configuration and all parameters, you should start with the full example. This allows you to define all race settings.

The quick-race example omits many parameters. This means you only override certain settings, while others remain as the user previously set them.

To avoid having to create separate templates for each championship or series of races, you can use placeholders. Take a look at the placeholders example for more information.

Here's how to pass your template to the launcher and start the race from your app:

```php
Path\To\Ams2Brocker\Launcher.exe -t “quick-race-example.json”
```

Here’s what it looks like with placeholders:

```php
Path\To\Ams2Brocker\Launcher.exe -t “placeholders-example.json”
-p “#CAR#=-950775810|#LIVERY#=70|#TRACK#=1910889511|#LAPS#=33”
```

Assign individual values to the placeholders for each race. Be sure to separate the individual placeholder/value pairs with the pipe symbol “|” without any spaces or line breaks in between:

```
#PH1#=12345|#PH2#=66|#PH3#=54321|#PH4#=33|...
```

The launcher accepts the following flags:

```
--template / -t
--placeholders / -p
```

Here are examples of how to implement AMS2Broker in different languages:

JavaScript (Electron/NodeJS):

```php
// Frontend
const { ipcRenderer } = require('electron');

document.getElementById('yourButton').addEventListener('click', () => {
  ipcRenderer.send('start-race');
});
```
```php
// Backend
const { ipcMain } = require('electron');
const { execFile } = require('child_process');

ipcMain.on('start-race', () => {
  execFile('Path\\To\\Ams2Brocker\\Launcher.exe',
    ['-t', 'quick-race-example.json'],
    (err) => {
      if (err) console.error(err);
    });
});
```

Vehicle and Track IDs:

IDs for vehicles and tracks can be retrieved via the AMS2 Dedicated Server. There are several response files in the Resources folder. These may be out of date. To obtain the IDs for the latest content, you can install the Dedicated Server via Steam (Tools) and run it locally. Once the server is running, you can retrieve the information using the following URIs:

http://localhost:9000/api/list/vehicles/
http://localhost:9000/api/list/vehicle_classes/
http://localhost:9000/api/list/tracks/
http://localhost:9000/api/list/liveries/

# USER GUIDE

As soon as the user clicks the button in your app and goes to the race, the launcher opens and starts the game. The first time this happens, Windows SmartScreen will likely pop up and block the launcher from running. The user must therefore be informed that they need to click “More Information” and then “Run anyway”.

Once the game reaches the main menu, the user enters the single-player race. There, they press the INSERT key on the keyboard, which patches the configuration. After confirming the process in the launcher, they can start the race.

## Troubleshooting:

There is a `config.ini` file in the Ams2-Broker directory. The path to Automobilista 2 can be entered there if the launcher cannot find the game automatically. Additionally, a different port can be set if the API that triggers the patching is unresponsive. Furthermore, the hotkeys can be changed there if their assignments conflict with other apps.









