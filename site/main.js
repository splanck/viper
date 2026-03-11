/* ==========================================================================
   Viper Platform Website — main.js
   Theme toggle, code tabs, mobile nav, syntax highlighting.
   ========================================================================== */

(function () {
  "use strict";

  /* ---------- Theme Toggle ---------- */
  var themeBtn = document.getElementById("theme-toggle");
  var html = document.documentElement;

  function setTheme(theme) {
    html.setAttribute("data-theme", theme);
    localStorage.setItem("viper-theme", theme);
    if (themeBtn) themeBtn.textContent = theme === "dark" ? "\u2600" : "\u263E";
  }

  // Init from stored preference or system preference
  var stored = localStorage.getItem("viper-theme");
  if (stored) {
    setTheme(stored);
  } else if (window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches) {
    setTheme("light");
  } else {
    setTheme("dark");
  }

  if (themeBtn) {
    themeBtn.addEventListener("click", function () {
      setTheme(html.getAttribute("data-theme") === "dark" ? "light" : "dark");
    });
  }

  /* ---------- Mobile Nav Toggle ---------- */
  var navToggle = document.querySelector(".nav-mobile-toggle");
  var navLinks = document.querySelector(".nav-links");

  if (navToggle && navLinks) {
    navToggle.textContent = "\u2630";
    navToggle.addEventListener("click", function () {
      navLinks.classList.toggle("open");
      navToggle.textContent = navLinks.classList.contains("open") ? "\u2715" : "\u2630";
    });
  }

  /* ---------- Code Tabs ---------- */
  document.querySelectorAll(".code-tabs").forEach(function (tabGroup) {
    var buttons = tabGroup.querySelectorAll(".code-tab-btn");
    var panels = tabGroup.querySelectorAll(".code-tab-panel");

    buttons.forEach(function (btn) {
      btn.addEventListener("click", function () {
        var target = btn.getAttribute("data-tab");
        buttons.forEach(function (b) { b.classList.remove("active"); });
        panels.forEach(function (p) { p.classList.remove("active"); });
        btn.classList.add("active");
        var panel = tabGroup.querySelector('[data-panel="' + target + '"]');
        if (panel) panel.classList.add("active");
      });
    });
  });

  /* ---------- Syntax Highlighting ---------- */
  var ZIA_KEYWORDS = /\b(module|bind|func|var|if|else|while|for|in|return|expose|hide|entity|final|as|true|false|new|try|catch|finally|throw|match|import|inherit)\b/g;
  var BASIC_KEYWORDS = /\b(DIM|AS|IF|THEN|ELSE|END|FOR|TO|NEXT|WHILE|WEND|FUNCTION|SUB|RETURN|CLASS|NEW|PRINT|INPUT|DO|LOOP|SELECT|CASE|NAMESPACE|USING|PROPERTY|GET|SET|BOOLEAN|INTEGER|STRING|DOUBLE|TRUE|FALSE|AND|OR|NOT|MOD|REM)\b/g;
  var IL_KEYWORDS = /\b(il|extern|global|const|func|entry_\w+|ret|call|br|brc|phi|alloca|store|load|iadd|isub|imul|idiv|imod|fadd|fsub|fmul|fdiv|icmp|fcmp|iadd\.ovf|isub\.ovf|imul\.ovf|const_str|const_int|const_float|sext|zext|trunc|itof|ftoi|i64|i32|i16|i8|f64|f32|str|void|bool|ptr)\b/g;

  function highlight(code, lang) {
    // Escape HTML
    var s = code.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

    // Comments
    if (lang === "zia" || lang === "il") {
      s = s.replace(/(\/\/.*)/g, '<span class="hl-comment">$1</span>');
    } else if (lang === "basic") {
      s = s.replace(/(\'.*|REM\b.*)/gi, '<span class="hl-comment">$1</span>');
    }

    // Strings
    s = s.replace(/("(?:[^"\\]|\\.)*")/g, '<span class="hl-string">$1</span>');

    // Numbers
    s = s.replace(/\b(\d+(?:\.\d+)?)\b/g, '<span class="hl-number">$1</span>');

    // Keywords
    if (lang === "zia") {
      s = s.replace(ZIA_KEYWORDS, '<span class="hl-keyword">$1</span>');
    } else if (lang === "basic") {
      s = s.replace(BASIC_KEYWORDS, '<span class="hl-keyword">$1</span>');
    } else if (lang === "il") {
      s = s.replace(IL_KEYWORDS, '<span class="hl-keyword">$1</span>');
      // IL directives/labels
      s = s.replace(/(@[\w.]+)/g, '<span class="hl-func">$1</span>');
      s = s.replace(/(%\w+)/g, '<span class="hl-type">$1</span>');
    }

    // Type annotations (Zia)
    if (lang === "zia") {
      s = s.replace(/\b(Integer|String|Boolean|Float|List|Map|Canvas|Pixels|Sprite)\b/g, '<span class="hl-type">$1</span>');
    }

    return s;
  }

  document.querySelectorAll("pre code[data-lang]").forEach(function (el) {
    var lang = el.getAttribute("data-lang");
    if (lang === "zia" || lang === "basic" || lang === "il") {
      el.innerHTML = highlight(el.textContent, lang);
    }
  });

})();
