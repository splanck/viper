// main.viper - Entry Point
// Part of SQLite Clone - ViperLang Implementation

module main;

import "./executor";

//=============================================================================
// MAIN ENTRY POINT
//=============================================================================

func main() {
    Viper.Terminal.Say("ViperSQL - SQLite Clone in ViperLang");
    Viper.Terminal.Say("Type SQL commands or 'exit' to quit.");
    Viper.Terminal.Say("");

    var exec = new Executor();
    exec.init();

    var running = true;
    while running {
        Viper.Terminal.Print("sql> ");
        var input = Viper.Terminal.ReadLine();

        if input == "exit" || input == "quit" {
            running = false;
            continue;
        }

        if input == "" {
            continue;
        }

        var result = exec.executeSql(input);
        Viper.Terminal.Say(result.toString());
        Viper.Terminal.Say("");
    }

    Viper.Terminal.Say("Goodbye!");
}
