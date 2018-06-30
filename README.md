# DiiBugger (WUPS version) [![Build Status](https://api.travis-ci.org/Maschell/DiiBuggerWUPS.svg?branch=master)](https://travis-ci.org/Maschell/DiiBuggerWUPS)

Allows you to connect to your console and use simple debug operations.
Checkout the CLI client by [jam1garner](https://github.com/jam1garner/diibugger-cli) for usage.  

Once the plugin was loaded, start the `diibugger-cli.py` with python3, and type in `connect [WIIU IP]` to connect to the console. When the running applications changes you need to reconnect.

# Wii U Plugin System
This is a plugin for the [Wii U Plugin System (WUPS)](https://github.com/Maschell/WiiUPluginSystem/). To be able to use this plugin you have to place the resulting `.mod` file into the following folder:

```
sd:/wiiu/plugins
```
When the file is placed on the SDCard you can load it with [plugin loader](https://github.com/Maschell/WiiUPluginSystem/).

## Building

For building you need: 
- [wups](https://github.com/Maschell/WiiUPluginSystem)
- [wut](https://github.com/decaf-emu/wut)
- [libutilswut](https://github.com/Maschell/libutils/tree/wut) (WUT version) for common functions.

Install them (in this order) according to their README's. Don't forget the dependencies of the libs itself.

## Credits
- Refactoring and porting to a WUPS/wut by Maschell
- Initially created by [Kinnay](https://github.com/Kinnay/DiiBugger)
- CLI client by [jam1garner](https://github.com/jam1garner/diibugger-cli)