# Viper Runtime Complementary API Report

**Analysis Date:** 2026-02-04
**Scope:** Complete survey of Viper.* namespace for NEW class opportunities
**Methodology:** Autonomous exploration of 260+ runtime files, 2,106 functions, 150 classes

---

## Executive Summary

### Current Runtime Statistics
- **Total Classes:** 150 across 37 namespaces
- **Total Functions:** 2,106
- **Implementation Files:** 260+ C/header files
- **Documentation:** 18 markdown files (13,054 lines)

### Recommended Additions
- **Total New Classes Recommended:** 127
- **High Priority (Critical Gaps):** 32
- **Medium Priority (Common Needs):** 48
- **Lower Priority (Specialized):** 47

### Top 20 Highest-Impact Additions

| Rank | Class | Namespace | Impact | Complexity |
|------|-------|-----------|--------|------------|
| 1 | `StringUtils` | Viper.Text | Critical | Low |
| 2 | `HttpClient` (with pooling) | Viper.Network | High | Medium |
| 3 | `AsyncTask` | Viper.Threads | High | Medium |
| 4 | `LruCache` | Viper.Collections | High | Low |
| 5 | `BitSet` | Viper.Collections | High | Low |
| 6 | `AudioMixer` | Viper.Sound | High | Medium |
| 7 | `InputMapper` | Viper.Input | High | Low |
| 8 | `SceneManager` | Viper.Game | High | Medium |
| 9 | `LayoutManager` | Viper.GUI | High | Medium |
| 10 | `ColorUtils` | Viper.Graphics | Medium | Low |
| 11 | `MultiMap` | Viper.Collections | Medium | Low |
| 12 | `ConcurrentQueue` | Viper.Collections | Medium | Medium |
| 13 | `Scheduler` | Viper.Time | Medium | Medium |
| 14 | `MessageBus` | Viper.Core | Medium | Medium |
| 15 | `JsonPath` | Viper.Text | Medium | Medium |
| 16 | `FormValidator` | Viper.GUI | Medium | Low |
| 17 | `Trie` | Viper.Collections | Medium | Medium |
| 18 | `PerlinNoise` | Viper.Math | Medium | Low |
| 19 | `SaveManager` | Viper.Game | Medium | Low |
| 20 | `Argon2` | Viper.Crypto | Medium | High |

---

## Part 1: Namespace Addition Inventory

---

### Viper.Text (NEW: 18 classes)

**Current State:** CSV, JSON, XML, YAML, Regex, Template, Scanner, StringBuilder, Guid, Codec, TextWrapper, CompiledPattern

#### High Priority Additions

**1. StringUtils** - Static utility class for common string operations
```
Complexity: Low | Impact: Critical
Justification: Missing fundamental operations every developer needs
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `StartsWith(s, prefix)` | `bool(str, str)` | Check if string starts with prefix |
| `EndsWith(s, suffix)` | `bool(str, str)` | Check if string ends with suffix |
| `Contains(s, substring)` | `bool(str, str)` | Check if string contains substring |
| `Replace(s, old, new)` | `str(str, str, str)` | Replace all occurrences (non-regex) |
| `ReplaceFirst(s, old, new)` | `str(str, str, str)` | Replace first occurrence |
| `Split(s, delimiter)` | `Seq(str, str)` | Split by string delimiter |
| `SplitN(s, delimiter, n)` | `Seq(str, str, i64)` | Split with max parts |
| `Join(seq, separator)` | `str(Seq, str)` | Join sequence with separator |
| `Repeat(s, count)` | `str(str, i64)` | Repeat string n times |
| `Reverse(s)` | `str(str)` | Reverse string characters |
| `PadLeft(s, width, char)` | `str(str, i64, str)` | Left-pad to width |
| `PadRight(s, width, char)` | `str(str, i64, str)` | Right-pad to width |
| `RemovePrefix(s, prefix)` | `str(str, str)` | Remove prefix if present |
| `RemoveSuffix(s, suffix)` | `str(str, str)` | Remove suffix if present |
| `Capitalize(s)` | `str(str)` | Capitalize first letter |
| `Title(s)` | `str(str)` | Title case (capitalize each word) |
| `Slug(s)` | `str(str)` | URL-safe slug (lowercase, hyphens) |

**2. StringSimilarity** - String comparison and fuzzy matching
```
Complexity: Medium | Impact: High
Justification: Essential for search, autocomplete, spell checking
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Levenshtein(a, b)` | `i64(str, str)` | Edit distance |
| `LevenshteinNorm(a, b)` | `f64(str, str)` | Normalized (0.0-1.0) |
| `JaroWinkler(a, b)` | `f64(str, str)` | Jaro-Winkler similarity |
| `Hamming(a, b)` | `i64(str, str)` | Hamming distance (same length) |
| `LongestCommonSubstring(a, b)` | `str(str, str)` | LCS |
| `FuzzyMatch(query, candidates)` | `Seq(str, Seq)` | Ranked fuzzy search |
| `SoundexCode(s)` | `str(str)` | Soundex phonetic code |

**3. Toml** - TOML configuration file support
```
Complexity: Medium | Impact: High
Justification: Common config format, complements JSON/YAML
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Parse(s)` | `obj(str)` | Parse TOML string to Map |
| `ParseFile(path)` | `obj(str)` | Parse TOML file |
| `Format(obj)` | `str(obj)` | Format Map as TOML |
| `IsValid(s)` | `bool(str)` | Validate TOML syntax |

**4. Ini** - INI/Config file support
```
Complexity: Low | Impact: Medium
Justification: Legacy format still widely used
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Parse(s)` | `Map(str)` | Parse INI to Map of Maps |
| `ParseFile(path)` | `Map(str)` | Parse INI file |
| `Format(map)` | `str(Map)` | Format as INI |
| `Get(ini, section, key)` | `str(Map, str, str)` | Get value |
| `Set(ini, section, key, val)` | `void(...)` | Set value |

**5. JsonPath** - JSONPath query expressions
```
Complexity: Medium | Impact: Medium
Justification: Query complex JSON without manual traversal
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Query(json, path)` | `obj(obj, str)` | Query with JSONPath |
| `QueryAll(json, path)` | `Seq(obj, str)` | Query all matches |
| `Set(json, path, value)` | `obj(...)` | Set value at path |
| `Delete(json, path)` | `obj(obj, str)` | Delete at path |

**6. Markdown** - Markdown to HTML conversion
```
Complexity: Medium | Impact: Medium
Justification: Documentation, rich text, content systems
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `ToHtml(md)` | `str(str)` | Convert Markdown to HTML |
| `ToPlainText(md)` | `str(str)` | Strip to plain text |
| `ExtractLinks(md)` | `Seq(str)` | Extract all links |
| `ExtractHeadings(md)` | `Seq(str)` | Extract headings |

#### Medium Priority Additions

**7. Diff** - Text difference computation
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Lines(a, b)` | `Seq(str, str)` | Line-by-line diff |
| `Words(a, b)` | `Seq(str, str)` | Word-by-word diff |
| `Patch(original, diff)` | `str(str, Seq)` | Apply diff patch |
| `UnifiedDiff(a, b, context)` | `str(...)` | Unified diff format |

**8. Html** - HTML parsing (tolerant of malformed HTML)
```
Complexity: High | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Parse(html)` | `obj(str)` | Parse HTML to DOM |
| `QuerySelector(doc, sel)` | `obj(obj, str)` | CSS selector query |
| `QuerySelectorAll(doc, sel)` | `Seq(obj, str)` | All matching elements |
| `GetText(node)` | `str(obj)` | Extract text content |
| `GetAttr(node, name)` | `str(obj, str)` | Get attribute |

**9. NumberFormat** - Locale-aware number formatting
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Format(n, decimals)` | `str(f64, i64)` | Format with decimals |
| `FormatCurrency(n, symbol)` | `str(f64, str)` | Currency format |
| `FormatPercent(n)` | `str(f64)` | Percentage format |
| `FormatWithSeparator(n, sep)` | `str(i64, str)` | Thousands separator |
| `ToWords(n)` | `str(i64)` | Number to words |
| `ToOrdinal(n)` | `str(i64)` | Ordinal (1st, 2nd, 3rd) |

**10. Pluralize** - English pluralization
```
Complexity: Low | Impact: Low
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Plural(word)` | `str(str)` | Pluralize noun |
| `Singular(word)` | `str(str)` | Singularize noun |
| `Count(n, word)` | `str(i64, str)` | "1 item" vs "2 items" |

**11-18. Additional Text Classes**
- **WordWrap** - Advanced hyphenation support
- **CharacterSet** - Unicode category operations
- **Transliterate** - Character transliteration
- **Sanitize** - HTML/SQL sanitization
- **Tokenizer** - Word/sentence tokenization
- **CaseConverter** - camelCase, snake_case, etc.
- **VersionParser** - Semantic version parsing
- **UrlTemplate** - RFC 6570 URI templates

---

### Viper.Collections (NEW: 15 classes)

**Current State:** Seq, List, Map, TreeMap, Set, Bag, SortedSet, Stack, Queue, Deque, Ring, Heap, Bytes, Grid2D, LazySeq

#### High Priority Additions

**1. LruCache** - Least Recently Used cache
```
Complexity: Low | Impact: High
Justification: Essential for caching, memoization
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(capacity)` | `LruCache(i64)` | Create with max size |
| `Get(key)` | `obj(str)` | Get value (moves to front) |
| `Put(key, value)` | `void(str, obj)` | Add/update (evicts oldest if full) |
| `Has(key)` | `bool(str)` | Check if key exists |
| `Remove(key)` | `void(str)` | Remove entry |
| `Clear()` | `void()` | Clear all entries |
| `Keys` | `Seq` | All keys (MRU to LRU order) |
| `Len` | `i64` | Current entry count |
| `Cap` | `i64` | Maximum capacity |

**2. BitSet** - Efficient boolean array
```
Complexity: Low | Impact: High
Justification: Memory-efficient flags, bloom filters, graphs
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(size)` | `BitSet(i64)` | Create with size bits |
| `Get(index)` | `bool(i64)` | Get bit value |
| `Set(index)` | `void(i64)` | Set bit to true |
| `Clear(index)` | `void(i64)` | Set bit to false |
| `Toggle(index)` | `void(i64)` | Toggle bit |
| `SetRange(start, end)` | `void(i64, i64)` | Set range to true |
| `ClearAll()` | `void()` | Clear all bits |
| `Count()` | `i64()` | Count set bits |
| `And(other)` | `BitSet(BitSet)` | Bitwise AND |
| `Or(other)` | `BitSet(BitSet)` | Bitwise OR |
| `Xor(other)` | `BitSet(BitSet)` | Bitwise XOR |
| `Not()` | `BitSet()` | Bitwise NOT |
| `Intersects(other)` | `bool(BitSet)` | Check if any common bits |

**3. MultiMap** - Key to multiple values mapping
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `MultiMap()` | Create empty |
| `Put(key, value)` | `void(str, obj)` | Add value to key's list |
| `Get(key)` | `Seq(str)` | Get all values for key |
| `GetFirst(key)` | `obj(str)` | Get first value |
| `Remove(key, value)` | `bool(str, obj)` | Remove specific value |
| `RemoveAll(key)` | `Seq(str)` | Remove all values for key |
| `Has(key)` | `bool(str)` | Check if key exists |
| `HasValue(key, value)` | `bool(str, obj)` | Check if key has value |
| `Keys` | `Seq` | All keys |
| `CountFor(key)` | `i64(str)` | Count values for key |
| `TotalCount` | `i64` | Total value count |

**4. BiMap** - Bidirectional map
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `BiMap()` | Create empty |
| `Put(key, value)` | `void(str, str)` | Add bidirectional mapping |
| `GetByKey(key)` | `str(str)` | Get value by key |
| `GetByValue(value)` | `str(str)` | Get key by value |
| `RemoveByKey(key)` | `void(str)` | Remove by key |
| `RemoveByValue(value)` | `void(str)` | Remove by value |
| `HasKey(key)` | `bool(str)` | Check key exists |
| `HasValue(value)` | `bool(str)` | Check value exists |
| `Inverse()` | `BiMap()` | Get inverse view |

**5. ConcurrentQueue** - Thread-safe queue
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `ConcurrentQueue()` | Create empty |
| `Enqueue(item)` | `void(obj)` | Thread-safe add |
| `TryDequeue()` | `Option(obj)` | Non-blocking dequeue |
| `Dequeue()` | `obj()` | Blocking dequeue |
| `DequeueFor(ms)` | `Option(obj, i64)` | Dequeue with timeout |
| `Peek()` | `Option(obj)` | Non-blocking peek |
| `Len` | `i64` | Approximate count |
| `IsEmpty` | `bool` | Check if empty |

**6. Trie** - Prefix tree for string keys
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `Trie()` | Create empty |
| `Put(key, value)` | `void(str, obj)` | Insert key-value |
| `Get(key)` | `obj(str)` | Get exact match |
| `Has(key)` | `bool(str)` | Check exact key |
| `HasPrefix(prefix)` | `bool(str)` | Any keys with prefix |
| `WithPrefix(prefix)` | `Seq(str)` | All keys with prefix |
| `Autocomplete(prefix, limit)` | `Seq(str, i64)` | Top completions |
| `Remove(key)` | `bool(str)` | Remove key |
| `LongestPrefix(s)` | `str(str)` | Longest matching prefix |

**7-15. Additional Collection Classes**
- **BloomFilter** - Probabilistic set membership
- **UnionFind** - Disjoint set for graph connectivity
- **SparseArray** - Memory-efficient sparse array
- **OrderedMap** - Insertion-order preserving map
- **CountMap** - Frequency counting map
- **DefaultMap** - Map with default value factory
- **FrozenSet** - Immutable set
- **FrozenMap** - Immutable map
- **WeakMap** - Weak reference map (if GC supports)

---

### Viper.Threads (NEW: 12 classes)

**Current State:** Thread, Monitor, Gate, Barrier, RwLock, SafeI64, Channel, Future, Promise, ThreadPool, Parallel

#### High Priority Additions

**1. AsyncTask** - Simplified async/await pattern
```
Complexity: Medium | Impact: High
Justification: Modern async programming without raw threads
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Run(fn)` | `Future(fn)` | Run function asynchronously |
| `RunOn(pool, fn)` | `Future(ThreadPool, fn)` | Run on specific pool |
| `All(futures)` | `Future(Seq)` | Wait for all |
| `Any(futures)` | `Future(Seq)` | Wait for first |
| `Race(futures)` | `Future(Seq)` | First completed, cancel rest |
| `Delay(ms, fn)` | `Future(i64, fn)` | Delayed execution |
| `Timeout(future, ms)` | `Future(Future, i64)` | Future with timeout |

**2. CancellationToken** - Cooperative cancellation
```
Complexity: Low | Impact: High
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `CancellationToken()` | Create token |
| `Cancel()` | `void()` | Request cancellation |
| `IsCancelled` | `bool` | Check if cancelled |
| `ThrowIfCancelled()` | `void()` | Trap if cancelled |
| `Register(callback)` | `void(fn)` | Register callback |
| `CreateLinked(parent)` | `CancellationToken(...)` | Child token |

**3. Scheduler** - Task scheduling
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `Scheduler()` | Create scheduler |
| `Schedule(delay, fn)` | `i64(i64, fn)` | One-shot after delay |
| `ScheduleRepeat(delay, interval, fn)` | `i64(...)` | Repeating task |
| `Cancel(taskId)` | `bool(i64)` | Cancel scheduled task |
| `Tick()` | `void()` | Process due tasks |
| `PendingCount` | `i64` | Number of pending tasks |

**4. Debouncer** - Debounce rapid calls
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(delayMs, fn)` | `Debouncer(i64, fn)` | Create debouncer |
| `Call()` | `void()` | Trigger (resets timer) |
| `CallWith(arg)` | `void(obj)` | Trigger with argument |
| `Flush()` | `void()` | Execute immediately |
| `Cancel()` | `void()` | Cancel pending execution |

**5. Throttler** - Rate limiting
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(intervalMs, fn)` | `Throttler(i64, fn)` | Create throttler |
| `Call()` | `bool()` | Try to call (returns if called) |
| `CallWith(arg)` | `bool(obj)` | Try to call with arg |
| `Reset()` | `void()` | Reset throttle timer |

**6-12. Additional Threading Classes**
- **CountDownLatch** - One-time synchronization barrier
- **Phaser** - Flexible multi-phase synchronization
- **StampedLock** - Optimistic read lock
- **ReadWriteSemaphore** - Multiple reader permits
- **AtomicRef** - Atomic reference wrapper
- **TaskGroup** - Structured concurrency
- **ProgressReporter** - Progress tracking for async tasks

---

### Viper.Time (NEW: 8 classes)

**Current State:** Clock, DateTime, Duration, Stopwatch, Countdown, DateOnly, Timer

#### High Priority Additions

**1. TimeZone** - Timezone handling
```
Complexity: High | Impact: High
Justification: Essential for international applications
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Local()` | `TimeZone()` | System local timezone |
| `Utc()` | `TimeZone()` | UTC timezone |
| `FromId(id)` | `TimeZone(str)` | From IANA ID (e.g., "America/New_York") |
| `Convert(dt, from, to)` | `i64(i64, TimeZone, TimeZone)` | Convert timestamp |
| `Offset(tz, dt)` | `i64(TimeZone, i64)` | Get UTC offset in seconds |
| `IsDst(tz, dt)` | `bool(TimeZone, i64)` | Check if DST active |
| `AllIds()` | `Seq()` | List all timezone IDs |

**2. CronSchedule** - Cron expression scheduling
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Parse(expr)` | `CronSchedule(str)` | Parse cron expression |
| `Next(from)` | `i64(i64)` | Next occurrence after timestamp |
| `NextN(from, n)` | `Seq(i64, i64)` | Next N occurrences |
| `Matches(dt)` | `bool(i64)` | Check if timestamp matches |
| `IsValid(expr)` | `bool(str)` | Validate expression |

**3. RelativeTime** - Human-readable relative times
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Format(dt)` | `str(i64)` | "2 hours ago", "in 3 days" |
| `FormatFrom(dt, reference)` | `str(i64, i64)` | Relative to reference |
| `FormatDuration(dur)` | `str(i64)` | "2h 30m", "1d 5h" |
| `FormatShort(dt)` | `str(i64)` | "2h", "3d" |

**4. DateRange** - Date interval type
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(start, end)` | `DateRange(i64, i64)` | Create range |
| `Contains(dt)` | `bool(i64)` | Check if date in range |
| `Overlaps(other)` | `bool(DateRange)` | Check overlap |
| `Intersection(other)` | `DateRange(DateRange)` | Get intersection |
| `Union(other)` | `DateRange(DateRange)` | Get union (if contiguous) |
| `Days()` | `i64()` | Number of days in range |
| `Iterate()` | `LazySeq()` | Iterate each day |

**5-8. Additional Time Classes**
- **RecurringEvent** - Repeating event definition
- **BusinessCalendar** - Working days, holidays
- **DurationParser** - Parse "1h30m", "2d"
- **Instant** - Nanosecond-precision timestamp

---

### Viper.Network (NEW: 10 classes)

**Current State:** Http, HttpReq, HttpRes, Tcp, TcpServer, Udp, Dns, Url, WebSocket, RestClient

#### High Priority Additions

**1. HttpClient** - Connection-pooling HTTP client
```
Complexity: Medium | Impact: High
Justification: Performance for multiple requests to same host
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `HttpClient()` | Create with default pool |
| `WithTimeout(ms)` | `HttpClient(i64)` | Set default timeout |
| `WithBaseUrl(url)` | `HttpClient(str)` | Set base URL |
| `WithHeader(name, value)` | `HttpClient(str, str)` | Add default header |
| `Get(url)` | `HttpRes(str)` | GET request |
| `Post(url, body)` | `HttpRes(str, obj)` | POST request |
| `GetJson(url)` | `obj(str)` | GET and parse JSON |
| `PostJson(url, obj)` | `obj(str, obj)` | POST JSON, return JSON |
| `Close()` | `void()` | Close all connections |

**2. RetryPolicy** - Retry with backoff
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Exponential(maxRetries, baseMs)` | `RetryPolicy(i64, i64)` | Exponential backoff |
| `Linear(maxRetries, delayMs)` | `RetryPolicy(i64, i64)` | Linear backoff |
| `Fixed(maxRetries, delayMs)` | `RetryPolicy(i64, i64)` | Fixed delay |
| `Execute(fn)` | `obj(fn)` | Execute with retry |
| `ExecuteAsync(fn)` | `Future(fn)` | Async with retry |

**3. RateLimiter** - Request rate limiting
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(requestsPerSecond)` | `RateLimiter(f64)` | Create limiter |
| `Acquire()` | `void()` | Block until allowed |
| `TryAcquire()` | `bool()` | Non-blocking check |
| `AcquireFor(ms)` | `bool(i64)` | Try with timeout |

**4. CookieJar** - Cookie management
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `CookieJar()` | Create empty jar |
| `Set(url, cookie)` | `void(str, str)` | Add cookie for URL |
| `Get(url)` | `str(str)` | Get cookies header for URL |
| `Clear()` | `void()` | Remove all cookies |
| `ClearFor(domain)` | `void(str)` | Remove cookies for domain |

**5. DownloadManager** - File downloads with progress
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `DownloadManager()` | Create manager |
| `Download(url, path)` | `Future(str, str)` | Download file |
| `DownloadWithProgress(url, path, callback)` | `Future(...)` | With progress |
| `Resume(url, path)` | `Future(str, str)` | Resume partial download |
| `Cancel(downloadId)` | `void(i64)` | Cancel download |

**6-10. Additional Network Classes**
- **FormData** - Multipart form builder
- **SseClient** - Server-Sent Events client
- **IpAddress** - IP address parsing/manipulation
- **PortScanner** - Port availability checking
- **ProxyConfig** - HTTP/SOCKS proxy configuration

---

### Viper.Crypto (NEW: 8 classes)

**Current State:** Cipher, Hash, KeyDerive, Rand, Password, Aes, Tls

#### High Priority Additions

**1. Argon2** - Modern password hashing
```
Complexity: High | Impact: High
Justification: Industry standard, memory-hard KDF
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Hash(password)` | `str(str)` | Hash with default params |
| `HashWithParams(password, memory, iterations, parallelism)` | `str(...)` | Custom params |
| `Verify(password, hash)` | `bool(str, str)` | Verify password |
| `NeedsRehash(hash)` | `bool(str)` | Check if params outdated |

**2. Jwt** - JSON Web Token support
```
Complexity: Medium | Impact: High
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Sign(payload, secret)` | `str(obj, str)` | Sign with HMAC-SHA256 |
| `SignWithKey(payload, key, alg)` | `str(...)` | Sign with specified alg |
| `Verify(token, secret)` | `obj(str, str)` | Verify and decode |
| `Decode(token)` | `obj(str)` | Decode without verify |
| `IsExpired(token)` | `bool(str)` | Check exp claim |

**3. Otp** - One-time password (TOTP/HOTP)
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `GenerateSecret()` | `str()` | Generate Base32 secret |
| `Totp(secret)` | `str(str)` | Generate current TOTP |
| `TotpAt(secret, time)` | `str(str, i64)` | TOTP at timestamp |
| `VerifyTotp(secret, code)` | `bool(str, str)` | Verify with window |
| `Hotp(secret, counter)` | `str(str, i64)` | Generate HOTP |
| `ProvisioningUri(secret, issuer, account)` | `str(...)` | QR code URI |

**4. Signature** - Digital signatures
```
Complexity: High | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `GenerateKeyPair()` | `(Bytes, Bytes)()` | Ed25519 keypair |
| `Sign(message, privateKey)` | `Bytes(Bytes, Bytes)` | Sign message |
| `Verify(message, signature, publicKey)` | `bool(...)` | Verify signature |

**5-8. Additional Crypto Classes**
- **SecureString** - Memory-wiped string
- **KeyStore** - Encrypted key storage
- **Checksum** - CRC16, Adler32, xxHash
- **Blake3** - Modern hash function

---

### Viper.Game (NEW: 15 classes)

**Current State:** Timer, Tween, StateMachine, SpriteAnimation, ParticleEmitter, ObjectPool, PathFollower, Quadtree, ScreenFX, Collision, CollisionRect, Grid2D, ButtonGroup, SmoothValue

#### High Priority Additions

**1. SceneManager** - Scene lifecycle management
```
Complexity: Medium | Impact: High
Justification: Every game needs scene transitions
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `SceneManager()` | Create manager |
| `Register(name, scene)` | `void(str, obj)` | Register scene |
| `Switch(name)` | `void(str)` | Switch to scene |
| `SwitchWithTransition(name, transition)` | `void(str, obj)` | With transition |
| `Push(name)` | `void(str)` | Push scene to stack |
| `Pop()` | `void()` | Pop and resume previous |
| `Current` | `obj` | Current scene |
| `Update(dt)` | `void(f64)` | Update current scene |
| `Render()` | `void()` | Render current scene |

**2. InputMapper** - Input action mapping
```
Complexity: Low | Impact: High
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `InputMapper()` | Create mapper |
| `Bind(action, key)` | `void(str, i64)` | Bind key to action |
| `BindPad(action, button)` | `void(str, i64)` | Bind gamepad button |
| `BindAxis(action, axis)` | `void(str, i64)` | Bind axis |
| `IsPressed(action)` | `bool(str)` | Check if pressed |
| `IsJustPressed(action)` | `bool(str)` | Check if just pressed |
| `IsJustReleased(action)` | `bool(str)` | Check if just released |
| `GetAxis(action)` | `f64(str)` | Get axis value (-1 to 1) |
| `Unbind(action)` | `void(str)` | Remove binding |
| `SaveBindings()` | `str()` | Serialize to JSON |
| `LoadBindings(json)` | `void(str)` | Load from JSON |

**3. SaveManager** - Game save/load
```
Complexity: Low | Impact: High
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(gameName)` | `SaveManager(str)` | Create for game |
| `Save(slot, data)` | `void(i64, obj)` | Save to slot |
| `Load(slot)` | `obj(i64)` | Load from slot |
| `Delete(slot)` | `void(i64)` | Delete save |
| `Exists(slot)` | `bool(i64)` | Check if slot exists |
| `ListSlots()` | `Seq()` | List all save slots |
| `GetInfo(slot)` | `Map(i64)` | Get save metadata |
| `AutoSave(data)` | `void(obj)` | Save to autosave slot |
| `LoadAutoSave()` | `obj()` | Load autosave |

**4. EntityPool** - ECS-lite entity management
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(capacity)` | `EntityPool(i64)` | Create pool |
| `Spawn()` | `i64()` | Create entity, return ID |
| `Destroy(id)` | `void(i64)` | Destroy entity |
| `IsAlive(id)` | `bool(i64)` | Check if entity exists |
| `SetComponent(id, name, value)` | `void(...)` | Set component |
| `GetComponent(id, name)` | `obj(i64, str)` | Get component |
| `HasComponent(id, name)` | `bool(i64, str)` | Check component |
| `Query(components)` | `Seq(Seq)` | Get entities with components |
| `ForEach(components, fn)` | `void(Seq, fn)` | Iterate matching entities |

**5. AStarPathfinder** - A* pathfinding
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(grid)` | `AStarPathfinder(Grid2D)` | Create for grid |
| `SetWalkable(value)` | `void(i64)` | Set walkable tile value |
| `SetBlocked(values)` | `void(Seq)` | Set blocked tile values |
| `FindPath(startX, startY, endX, endY)` | `Seq(...)` | Find path |
| `FindPathDiagonal(...)` | `Seq(...)` | Allow diagonal movement |
| `SetCost(value, cost)` | `void(i64, f64)` | Set tile movement cost |

**6-15. Additional Game Classes**
- **DialogueSystem** - Branching dialogue trees
- **AchievementManager** - Achievement tracking
- **LeaderboardLocal** - Local high scores
- **ComboDetector** - Input combo detection
- **CameraShake** - Advanced shake effects
- **ParallaxLayer** - Parallax scrolling helper
- **SpawnManager** - Wave/spawn timing
- **InventoryGrid** - Grid-based inventory
- **CraftingSystem** - Recipe-based crafting
- **QuestTracker** - Quest state tracking

---

### Viper.GUI (NEW: 15 classes)

**Current State:** App, Button, Label, TextInput, Checkbox, RadioButton, RadioGroup, Slider, ProgressBar, Spinner, Dropdown, ListBox, Image, VBox, HBox, ScrollView, TabBar, TreeView, SplitPane, CodeEditor + several others

#### High Priority Additions

**1. LayoutManager** - Advanced layout system
```
Complexity: Medium | Impact: High
Justification: Complex UIs need flexible layouts
```
| Type | Description |
|------|-------------|
| `FlowLayout` | Wrap items to next line |
| `GridLayout` | Fixed grid cells |
| `DockLayout` | Top/bottom/left/right/center |
| `AnchorLayout` | Anchor to parent edges |
| `StackLayout` | Overlay stack |

**2. FormValidator** - Form validation framework
```
Complexity: Low | Impact: High
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `FormValidator()` | Create validator |
| `AddRequired(field, message)` | `void(Widget, str)` | Required validation |
| `AddPattern(field, regex, message)` | `void(...)` | Regex validation |
| `AddRange(field, min, max, message)` | `void(...)` | Numeric range |
| `AddCustom(field, fn, message)` | `void(...)` | Custom validator |
| `Validate()` | `bool()` | Run all validations |
| `GetErrors()` | `Map()` | Get error messages |
| `ClearErrors()` | `void()` | Clear all errors |

**3. DataBinding** - Model-view binding
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Bind(widget, property, model, path)` | `void(...)` | Two-way bind |
| `BindOneWay(widget, property, model, path)` | `void(...)` | One-way bind |
| `Unbind(widget, property)` | `void(Widget, str)` | Remove binding |
| `Refresh(model)` | `void(obj)` | Force refresh |

**4. AnimationController** - Widget animations
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `FadeIn(widget, durationMs)` | `Future(Widget, i64)` | Fade in |
| `FadeOut(widget, durationMs)` | `Future(Widget, i64)` | Fade out |
| `SlideIn(widget, direction, durationMs)` | `Future(...)` | Slide in |
| `Scale(widget, from, to, durationMs)` | `Future(...)` | Scale animation |
| `Sequence(animations)` | `Future(Seq)` | Run in sequence |
| `Parallel(animations)` | `Future(Seq)` | Run in parallel |

**5-15. Additional GUI Classes**
- **DragDropManager** - Drag and drop coordination
- **UndoManager** - Undo/redo command stack
- **ToastManager** - Notification toasts
- **ModalManager** - Modal dialog stack
- **ThemeBuilder** - Custom theme creation
- **KeyboardShortcuts** - Global keyboard shortcuts
- **Wizard** - Multi-step wizard dialog
- **PropertyGrid** - Object property editor
- **DataGrid** - Spreadsheet-like grid
- **Chart** - Simple charts (bar, line, pie)
- **RichTextEditor** - Formatted text editing

---

### Viper.Graphics (NEW: 10 classes)

**Current State:** Canvas, Pixels, Color, Sprite, SpriteBatch, Tilemap, Camera, Scene, SceneNode

#### High Priority Additions

**1. ColorUtils** - Color manipulation utilities
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `FromHex(hex)` | `i64(str)` | Parse "#RRGGBB" or "#RRGGBBAA" |
| `ToHex(color)` | `str(i64)` | Convert to hex string |
| `FromHsl(h, s, l)` | `i64(f64, f64, f64)` | HSL to RGB |
| `ToHsl(color)` | `(f64, f64, f64)(i64)` | RGB to HSL |
| `Lighten(color, amount)` | `i64(i64, f64)` | Lighten by percentage |
| `Darken(color, amount)` | `i64(i64, f64)` | Darken by percentage |
| `Saturate(color, amount)` | `i64(i64, f64)` | Increase saturation |
| `Desaturate(color, amount)` | `i64(i64, f64)` | Decrease saturation |
| `Mix(color1, color2, ratio)` | `i64(i64, i64, f64)` | Blend colors |
| `Complement(color)` | `i64(i64)` | Complementary color |
| `Palette(base, count)` | `Seq(i64, i64)` | Generate palette |

**2. TextureAtlas** - Sprite atlas management
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Load(imagePath, dataPath)` | `TextureAtlas(str, str)` | Load atlas + data |
| `GetRegion(name)` | `obj(str)` | Get named region |
| `GetAnimation(prefix)` | `Seq(str)` | Get animation frames |
| `Draw(canvas, name, x, y)` | `void(...)` | Draw region |
| `DrawScaled(canvas, name, x, y, scale)` | `void(...)` | Draw scaled |

**3. ShapeBuilder** - Complex shape construction
```
Complexity: Low | Impact: Low
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `RoundedRect(x, y, w, h, radius)` | `Seq(...)` | Rounded rectangle points |
| `Star(cx, cy, points, outerR, innerR)` | `Seq(...)` | Star polygon |
| `Arrow(x1, y1, x2, y2, headSize)` | `Seq(...)` | Arrow shape |
| `RegularPolygon(cx, cy, sides, radius)` | `Seq(...)` | Regular polygon |
| `Arc(cx, cy, radius, startAngle, endAngle)` | `Seq(...)` | Arc points |

**4-10. Additional Graphics Classes**
- **Gradient** - Gradient generators (linear, radial)
- **ImageFilter** - Blur, sharpen, grayscale filters
- **NinePatch** - 9-patch image scaling
- **BitmapFont** - Custom bitmap font support
- **ParticlePresets** - Common particle effects
- **TilesetBuilder** - Tileset from image
- **SpriteSheet** - Uniform sprite sheet

---

### Viper.Sound (NEW: 8 classes)

**Current State:** Audio, Sound, Music, Voice, Playlist

#### High Priority Additions

**1. AudioMixer** - Audio mixing and routing
```
Complexity: Medium | Impact: High
Justification: Essential for game audio management
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `AudioMixer()` | Create mixer |
| `CreateChannel(name)` | `void(str)` | Create named channel |
| `SetChannelVolume(name, volume)` | `void(str, i64)` | Set channel volume |
| `MuteChannel(name)` | `void(str)` | Mute channel |
| `UnmuteChannel(name)` | `void(str)` | Unmute channel |
| `PlayOn(sound, channel)` | `i64(Sound, str)` | Play on channel |
| `SetMasterVolume(volume)` | `void(i64)` | Set master volume |
| `FadeChannel(name, to, durationMs)` | `void(...)` | Fade channel |

**2. SoundPool** - Pooled sound playback
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(sound, poolSize)` | `SoundPool(Sound, i64)` | Create pool |
| `Play()` | `void()` | Play from pool |
| `PlayAt(volume, pan)` | `void(i64, i64)` | Play with settings |
| `StopAll()` | `void()` | Stop all playing |
| `ActiveCount` | `i64` | Number playing |

**3. MusicCrossfader** - Crossfade between tracks
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `MusicCrossfader()` | Create crossfader |
| `Play(music)` | `void(Music)` | Play with crossfade |
| `SetDuration(ms)` | `void(i64)` | Set crossfade duration |
| `SetCurve(curve)` | `void(str)` | Set fade curve |

**4-8. Additional Sound Classes**
- **Jukebox** - Automatic music management
- **SoundGroup** - Random sound selection
- **AudioVisualizer** - FFT visualization data
- **DuckingController** - Audio ducking
- **LoopRegion** - Seamless loop points

---

### Viper.Math (NEW: 12 classes)

**Current State:** Vec2, Vec3, Mat3, Mat4, Random, Bits, Math (static)

#### High Priority Additions

**1. PerlinNoise** - Procedural noise generation
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(seed)` | `PerlinNoise(i64)` | Create with seed |
| `Noise2D(x, y)` | `f64(f64, f64)` | 2D noise (-1 to 1) |
| `Noise3D(x, y, z)` | `f64(f64, f64, f64)` | 3D noise |
| `Octave2D(x, y, octaves, persistence)` | `f64(...)` | Fractal noise |
| `Turbulence2D(x, y, octaves)` | `f64(...)` | Turbulence |

**2. Rect** - Rectangle type
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(x, y, w, h)` | `Rect(f64, f64, f64, f64)` | Create rectangle |
| `FromPoints(x1, y1, x2, y2)` | `Rect(...)` | From corners |
| `Contains(x, y)` | `bool(f64, f64)` | Point containment |
| `ContainsRect(other)` | `bool(Rect)` | Rectangle containment |
| `Intersects(other)` | `bool(Rect)` | Intersection test |
| `Intersection(other)` | `Rect(Rect)` | Get intersection |
| `Union(other)` | `Rect(Rect)` | Get bounding rect |
| `Expand(amount)` | `Rect(f64)` | Expand/shrink |
| `Center` | `(f64, f64)` | Center point |

**3. Circle** - Circle type
```
Complexity: Low | Impact: Low
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(x, y, radius)` | `Circle(f64, f64, f64)` | Create circle |
| `Contains(x, y)` | `bool(f64, f64)` | Point containment |
| `Intersects(other)` | `bool(Circle)` | Circle intersection |
| `IntersectsRect(rect)` | `bool(Rect)` | Rect intersection |
| `BoundingRect` | `Rect` | Get bounding rect |

**4. Bezier** - Bezier curve evaluation
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Quadratic(p0, p1, p2, t)` | `(f64,f64)(...)` | Quadratic bezier |
| `Cubic(p0, p1, p2, p3, t)` | `(f64,f64)(...)` | Cubic bezier |
| `QuadraticLength(p0, p1, p2)` | `f64(...)` | Approximate length |
| `CubicLength(p0, p1, p2, p3)` | `f64(...)` | Approximate length |
| `Split(t)` | `(Bezier, Bezier)(f64)` | Split at t |

**5. Easing** - Easing function library
```
Complexity: Low | Impact: Medium
Note: Complements Tween class with more options
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Linear(t)` | `f64(f64)` | Linear |
| `InQuad(t)` | `f64(f64)` | Quadratic ease in |
| `OutQuad(t)` | `f64(f64)` | Quadratic ease out |
| `InOutQuad(t)` | `f64(f64)` | Quadratic ease in-out |
| `InCubic(t)` | `f64(f64)` | Cubic ease in |
| ... | ... | (All standard easings) |
| `Custom(fn)` | `fn(fn)` | Custom easing function |
| `Chain(ease1, ease2, t)` | `f64(fn, fn, f64)` | Chain two easings |

**6-12. Additional Math Classes**
- **Quaternion** - 3D rotation quaternions
- **Spline** - Catmull-Rom spline interpolation
- **Statistics** - Mean, median, stddev, percentiles
- **Distribution** - Gaussian, Poisson distributions
- **FixedPoint** - Fixed-point arithmetic
- **Angle** - Degree/radian conversion with operations
- **Transform2D** - 2D transformation matrix builder

---

### Viper.IO (NEW: 8 classes)

**Current State:** File, Dir, Path, BinFile, MemStream, Stream, LineReader, LineWriter, Archive, Compress, Glob, TempFile, Watcher

#### Medium Priority Additions

**1. AtomicFile** - Atomic file writes
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Write(path, content)` | `void(str, str)` | Atomic text write |
| `WriteBytes(path, bytes)` | `void(str, Bytes)` | Atomic binary write |
| `WriteWith(path, fn)` | `void(str, fn)` | Write via callback |

**2. FileLock** - File locking
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Acquire(path)` | `FileLock(str)` | Acquire exclusive lock |
| `TryAcquire(path)` | `FileLock(str)` | Try acquire (null if locked) |
| `AcquireShared(path)` | `FileLock(str)` | Shared/read lock |
| `Release()` | `void()` | Release lock |
| `IsLocked(path)` | `bool(str)` | Check if locked |

**3. BufferedReader** - Buffered text reading
```
Complexity: Low | Impact: Low
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(stream, bufferSize)` | `BufferedReader(...)` | Wrap stream |
| `ReadLine()` | `str()` | Read line |
| `ReadChar()` | `i64()` | Read character |
| `PeekChar()` | `i64()` | Peek character |
| `Unread(char)` | `void(i64)` | Push back character |

**4-8. Additional IO Classes**
- **ConfigFile** - Simple key-value config files
- **TarArchive** - TAR format support
- **FileTree** - Recursive directory walker
- **PathMatcher** - Advanced glob patterns
- **TempDir** - Temporary directory with cleanup

---

### Viper.Core (NEW: 10 classes)

**Proposed NEW namespace for cross-cutting utilities**

#### High Priority Additions

**1. MessageBus** - Pub/sub event system
```
Complexity: Medium | Impact: High
Justification: Decoupled communication between components
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Global()` | `MessageBus()` | Get global bus |
| `New()` | `MessageBus()` | Create local bus |
| `Subscribe(topic, handler)` | `i64(str, fn)` | Subscribe to topic |
| `Unsubscribe(subscriptionId)` | `void(i64)` | Unsubscribe |
| `Publish(topic, message)` | `void(str, obj)` | Publish message |
| `PublishAsync(topic, message)` | `Future(str, obj)` | Async publish |
| `HasSubscribers(topic)` | `bool(str)` | Check if topic has subscribers |

**2. ServiceLocator** - Simple DI container
```
Complexity: Low | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `Global()` | `ServiceLocator()` | Get global locator |
| `Register(name, instance)` | `void(str, obj)` | Register service |
| `RegisterFactory(name, factory)` | `void(str, fn)` | Register factory |
| `Get(name)` | `obj(str)` | Get service |
| `TryGet(name)` | `Option(obj, str)` | Try get service |
| `Has(name)` | `bool(str)` | Check if registered |

**3. CommandQueue** - Command pattern with undo
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New(maxHistory)` | `CommandQueue(i64)` | Create queue |
| `Execute(command)` | `void(obj)` | Execute and record |
| `Undo()` | `bool()` | Undo last command |
| `Redo()` | `bool()` | Redo undone command |
| `CanUndo` | `bool` | Check if undo available |
| `CanRedo` | `bool` | Check if redo available |
| `Clear()` | `void()` | Clear history |

**4. StateMachineBuilder** - Fluent state machine builder
```
Complexity: Medium | Impact: Medium
```
| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `StateMachineBuilder()` | Create builder |
| `State(name)` | `StateBuilder(str)` | Define state |
| `OnEnter(fn)` | `StateBuilder(fn)` | Enter callback |
| `OnExit(fn)` | `StateBuilder(fn)` | Exit callback |
| `OnUpdate(fn)` | `StateBuilder(fn)` | Update callback |
| `Transition(event, target)` | `StateBuilder(str, str)` | Add transition |
| `TransitionIf(event, target, guard)` | `StateBuilder(...)` | Conditional transition |
| `Build()` | `StateMachine()` | Build state machine |

**5-10. Additional Core Classes**
- **ObjectPool** - Generic object pooling
- **ResourceHandle** - RAII-style resource management
- **Disposable** - IDisposable pattern helper
- **WeakRef** - Weak reference wrapper
- **Memo** - Memoization helper
- **Retry** - Generic retry logic

---

## Part 2: Implementation Priority Matrix

### Phase 1: Critical Infrastructure (Weeks 1-4)
| Class | Namespace | Effort | Dependencies |
|-------|-----------|--------|--------------|
| StringUtils | Viper.Text | 2 days | None |
| LruCache | Viper.Collections | 2 days | None |
| BitSet | Viper.Collections | 2 days | None |
| AsyncTask | Viper.Threads | 3 days | Future |
| MessageBus | Viper.Core | 3 days | None |
| InputMapper | Viper.Input | 3 days | None |

### Phase 2: High-Value Additions (Weeks 5-8)
| Class | Namespace | Effort | Dependencies |
|-------|-----------|--------|--------------|
| HttpClient (pooling) | Viper.Network | 5 days | Http |
| SceneManager | Viper.Game | 4 days | Scene |
| AudioMixer | Viper.Sound | 5 days | Audio |
| LayoutManager | Viper.GUI | 5 days | Widget |
| TimeZone | Viper.Time | 5 days | DateTime |
| ColorUtils | Viper.Graphics | 2 days | Color |

### Phase 3: Extended Functionality (Weeks 9-12)
| Class | Namespace | Effort | Dependencies |
|-------|-----------|--------|--------------|
| Argon2 | Viper.Crypto | 1 week | None |
| Jwt | Viper.Crypto | 3 days | Hash |
| StringSimilarity | Viper.Text | 3 days | None |
| Trie | Viper.Collections | 3 days | None |
| ConcurrentQueue | Viper.Collections | 3 days | Monitor |
| SaveManager | Viper.Game | 3 days | Json, File |

### Phase 4: Polish & Specialized (Weeks 13+)
- All remaining classes by namespace priority

---

## Part 3: Implementation Notes

### Design Principles for New Classes

1. **Composability:** New classes should work with existing primitives
2. **Consistency:** Follow existing naming conventions (Get/Set, Is/Has, New/Create)
3. **Immutability where sensible:** Return new objects rather than mutate
4. **Error handling:** Use Option/Result for fallible operations, trap for programmer errors
5. **Thread safety:** Document thread safety guarantees

### Runtime Integration Checklist

For each new class:
- [ ] Header file (rt_<name>.h)
- [ ] Implementation file (rt_<name>.c)
- [ ] Add to CMakeLists.txt (appropriate source group)
- [ ] Add to runtime.def (RT_CLASS_BEGIN, RT_FUNC entries)
- [ ] Add to RuntimeClasses.hpp (RTCLS_<Name> enum)
- [ ] Add to RuntimeSignatures.cpp (function pointer mappings)
- [ ] Unit tests (RTXxxTests.cpp)
- [ ] Documentation (docs/viperlib/<namespace>.md)

### Testing Requirements

- Unit tests for all public methods
- Edge case coverage (empty inputs, null handling, boundary conditions)
- Thread safety tests for concurrent classes
- Performance benchmarks for critical paths

---

## Appendix A: Classes by Complexity

### Low Complexity (1-3 days each)
StringUtils, LruCache, BitSet, RelativeTime, ColorUtils, Pluralize, AtomicFile, Debouncer, Throttler, Easing, Rect, Circle, SoundPool, FormValidator, Ini

### Medium Complexity (3-7 days each)
AsyncTask, HttpClient, SceneManager, InputMapper, MessageBus, TimeZone, Trie, ConcurrentQueue, AudioMixer, LayoutManager, SaveManager, JsonPath, CronSchedule, Jwt, Otp, StateMachineBuilder

### High Complexity (1-2 weeks each)
Argon2, Html, TimeZone (with full IANA database), DataBinding, TextureAtlas, AStarPathfinder, EntityPool

---

## Appendix B: External Dependencies

Most classes require **no external dependencies** and can be implemented in pure C.

Classes that may benefit from optional external libraries:
- **Argon2:** Reference implementation (public domain)
- **Html:** Could use simplified parser or optional htmlparser2 binding
- **TimeZone:** IANA timezone database (public domain)
- **PerlinNoise:** Reference implementation available (public domain)

---

*Report generated by autonomous analysis of Viper runtime codebase*
*Total classes analyzed: 150 existing + 127 recommended = 277 potential classes*
