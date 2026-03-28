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
  var ZIA_KEYWORDS = /\b(module|bind|func|var|if|else|while|for|in|return|expose|hide|class|struct|final|as|true|false|new|try|catch|finally|throw|match|import|inherit)\b/g;
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

  /* ---------- Scroll Reveal (IntersectionObserver) ---------- */
  var revealElements = document.querySelectorAll(".reveal");
  if (revealElements.length > 0 && "IntersectionObserver" in window) {
    var revealObserver = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) {
          entry.target.classList.add("revealed");
          revealObserver.unobserve(entry.target);
        }
      });
    }, { threshold: 0.1, rootMargin: "0px 0px -40px 0px" });

    revealElements.forEach(function (el) {
      // Stagger children within reveal-stagger parents
      if (el.parentElement && el.parentElement.classList.contains("reveal-stagger")) {
        var siblings = el.parentElement.querySelectorAll(".reveal");
        var sibIndex = Array.prototype.indexOf.call(siblings, el);
        el.style.transitionDelay = (sibIndex * 0.08) + "s";
      }
      revealObserver.observe(el);
    });
  } else {
    // Fallback: show everything immediately
    revealElements.forEach(function (el) { el.classList.add("revealed"); });
  }

  /* ---------- Stats Counter Animation ---------- */
  var statValues = document.querySelectorAll(".stat-value");
  var statsAnimated = false;

  function parseStatTarget(text) {
    var match = text.trim().match(/^([^\d]*)(\d[\d,]*)(.*)$/);
    if (!match) return null;
    return {
      prefix: match[1],
      number: parseInt(match[2].replace(/,/g, ""), 10),
      suffix: match[3],
      hasComma: match[2].indexOf(",") !== -1
    };
  }

  function formatNumber(n, useComma) {
    if (!useComma) return String(n);
    return n.toLocaleString("en-US");
  }

  function animateCounters() {
    if (statsAnimated) return;
    statsAnimated = true;
    var duration = 1500;

    statValues.forEach(function (el) {
      var parsed = parseStatTarget(el.textContent);
      if (!parsed || parsed.number === 0) return;
      var start = null;
      var target = parsed.number;

      function step(timestamp) {
        if (!start) start = timestamp;
        var progress = Math.min((timestamp - start) / duration, 1);
        var eased = 1 - Math.pow(1 - progress, 3);
        var current = Math.round(eased * target);
        el.textContent = parsed.prefix + formatNumber(current, parsed.hasComma) + parsed.suffix;
        if (progress < 1) requestAnimationFrame(step);
      }

      el.textContent = parsed.prefix + "0" + parsed.suffix;
      requestAnimationFrame(step);
    });
  }

  var statsBar = document.querySelector(".stats-bar");
  if (statsBar && "IntersectionObserver" in window) {
    var statsObserver = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) {
          animateCounters();
          statsObserver.unobserve(entry.target);
        }
      });
    }, { threshold: 0.3 });
    statsObserver.observe(statsBar);
  }

  /* ---------- Showcase Filtering ---------- */
  var filterBtns = document.querySelectorAll(".filter-btn");
  if (filterBtns.length > 0) {
    filterBtns.forEach(function (btn) {
      btn.addEventListener("click", function () {
        var filter = btn.getAttribute("data-filter");
        var cards = document.querySelectorAll(".showcase-card[data-tags]");

        // Update active button
        filterBtns.forEach(function (b) { b.classList.remove("active"); });
        btn.classList.add("active");

        // Filter cards
        cards.forEach(function (card) {
          var tags = card.getAttribute("data-tags") || "";
          if (filter === "all" || tags.indexOf(filter) !== -1) {
            card.style.display = "";
          } else {
            card.style.display = "none";
          }
        });
      });
    });
  }

  /* ---------- Sidebar Scroll Spy ---------- */
  var sidebarLinks = document.querySelectorAll(".page-sidebar a[href^='#']");
  if (sidebarLinks.length > 0 && "IntersectionObserver" in window) {
    var spyTargets = [];
    sidebarLinks.forEach(function (link) {
      var id = link.getAttribute("href").slice(1);
      var el = document.getElementById(id);
      if (el) spyTargets.push({ el: el, link: link });
    });

    var spyObserver = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) {
          sidebarLinks.forEach(function (l) { l.classList.remove("active"); });
          var match = spyTargets.find(function (t) { return t.el === entry.target; });
          if (match) match.link.classList.add("active");
        }
      });
    }, { rootMargin: "-20% 0px -70% 0px" });

    spyTargets.forEach(function (t) { spyObserver.observe(t.el); });
  }

  /* ---------- API Search ---------- */
  var apiSearch = document.querySelector(".api-search");
  if (apiSearch) {
    apiSearch.addEventListener("input", function () {
      var query = apiSearch.value.toLowerCase();
      var rows = document.querySelectorAll(".api-table tbody tr");
      rows.forEach(function (row) {
        var text = row.textContent.toLowerCase();
        row.style.display = text.indexOf(query) !== -1 ? "" : "none";
      });
    });
  }

  /* ---------- Nav Scroll Shadow ---------- */
  var nav = document.querySelector(".top-nav");
  if (nav) {
    window.addEventListener("scroll", function () {
      if (window.scrollY > 20) {
        nav.classList.add("nav-scrolled");
      } else {
        nav.classList.remove("nav-scrolled");
      }
    }, { passive: true });
  }

})();
