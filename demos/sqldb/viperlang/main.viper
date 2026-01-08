// SQLite Clone - Main Entry Point
// ViperLang Implementation

module main;

import "./token";
import "./lexer";

func start() {
    Viper.Terminal.Say("SQLite Clone - ViperLang Edition");
    Viper.Terminal.Say("================================");
    Viper.Terminal.Say("");

    // Run token tests
    token.testTokens();
    Viper.Terminal.Say("");

    // Run lexer tests
    lexer.testLexer();
}
