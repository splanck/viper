; Comprehensive test suite for the Lisp interpreter

; === Test helper ===
(define pass-count 0)
(define fail-count 0)

(define assert
  (lambda (label result expected)
    (if (= result expected)
        (begin
          (set! pass-count (+ pass-count 1))
          (display "  PASS: ")
          (display label)
          (newline))
        (begin
          (set! fail-count (+ fail-count 1))
          (display "  FAIL: ")
          (display label)
          (display " expected=")
          (display expected)
          (display " got=")
          (display result)
          (newline)))))

; === Phase 1: Arithmetic ===
(display "--- Arithmetic ---")
(newline)
(assert "addition" (+ 1 2) 3)
(assert "subtraction" (- 10 3) 7)
(assert "multiplication" (* 4 5) 20)
(assert "division" (/ 10 2) 5)
(assert "modulo" (mod 10 3) 1)
(assert "negate" (- 5) -5)
(assert "variadic +" (+ 1 2 3 4) 10)
(assert "variadic *" (* 1 2 3 4) 24)
(assert "nested" (+ (* 2 3) (- 10 4)) 12)

; === Phase 2: Comparison ===
(display "--- Comparison ---")
(newline)
(assert "less-than true" (< 1 2) #t)
(assert "less-than false" (< 2 1) #f)
(assert "greater-than" (> 5 3) #t)
(assert "equal" (= 42 42) #t)
(assert "not-equal" (= 1 2) #f)
(assert "leq" (<= 3 3) #t)
(assert "geq" (>= 5 3) #t)

; === Phase 3: Boolean logic ===
(display "--- Boolean logic ---")
(newline)
(assert "and-true" (and #t #t) #t)
(assert "and-false" (and #t #f) #f)
(assert "or-true" (or #f #t) #t)
(assert "or-false" (or #f #f) #f)
(assert "not-true" (not #t) #f)
(assert "not-false" (not #f) #t)

; === Phase 4: Define and lambda ===
(display "--- Define and lambda ---")
(newline)
(define x 42)
(assert "define" x 42)

(define square (lambda (x) (* x x)))
(assert "lambda" (square 5) 25)

(define add (lambda (a b) (+ a b)))
(assert "two-arg lambda" (add 3 7) 10)

; === Phase 5: Recursion ===
(display "--- Recursion ---")
(newline)
(define fact
  (lambda (n)
    (if (< n 2) 1 (* n (fact (- n 1))))))
(assert "factorial 0" (fact 0) 1)
(assert "factorial 1" (fact 1) 1)
(assert "factorial 5" (fact 5) 120)
(assert "factorial 10" (fact 10) 3628800)

(define fib
  (lambda (n)
    (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))
(assert "fibonacci 0" (fib 0) 0)
(assert "fibonacci 1" (fib 1) 1)
(assert "fibonacci 10" (fib 10) 55)

; === Phase 6: List operations ===
(display "--- List operations ---")
(newline)
(assert "cons/car" (car (cons 1 2)) 1)
(assert "cons/cdr" (cdr (cons 1 2)) 2)
(assert "list" (car (list 1 2 3)) 1)
(assert "list-cdr" (car (cdr (list 1 2 3))) 2)
(assert "null?-nil" (null? '()) #t)
(assert "null?-pair" (null? (cons 1 2)) #f)
(assert "length" (length (list 1 2 3)) 3)

; === Phase 7: Quote ===
(display "--- Quote ---")
(newline)
(assert "quote-num" (car '(1 2 3)) 1)
(assert "quote-sym" (car '(a b c)) 'a)

; === Phase 8: Closures ===
(display "--- Closures ---")
(newline)
(define make-adder
  (lambda (x)
    (lambda (y) (+ x y))))
(define add5 (make-adder 5))
(assert "closure" (add5 10) 15)
(assert "closure 2" (add5 20) 25)

(define make-counter
  (lambda ()
    (begin
      (define count 0)
      (lambda ()
        (begin
          (set! count (+ count 1))
          count)))))

; === Phase 9: Let / Let* ===
(display "--- Let ---")
(newline)
(assert "let" (let ((x 10) (y 20)) (+ x y)) 30)
(assert "let*" (let* ((x 10) (y (* x 2))) (+ x y)) 30)

; === Phase 10: Cond ===
(display "--- Cond ---")
(newline)
(define classify
  (lambda (n)
    (cond
      ((< n 0) -1)
      ((= n 0) 0)
      (else 1))))
(assert "cond neg" (classify -5) -1)
(assert "cond zero" (classify 0) 0)
(assert "cond pos" (classify 5) 1)

; === Phase 11: Type predicates ===
(display "--- Type predicates ---")
(newline)
(assert "number?" (number? 42) #t)
(assert "symbol?" (symbol? 'foo) #t)
(assert "pair?" (pair? (cons 1 2)) #t)
(assert "boolean?" (boolean? #t) #t)
(assert "null?-check" (null? '()) #t)

; === Phase 12: Higher-order functions ===
(display "--- Higher-order ---")
(newline)
(assert "map" (car (map (lambda (x) (* x x)) (list 1 2 3))) 1)
(assert "map-second" (car (cdr (map (lambda (x) (* x x)) (list 1 2 3)))) 4)

; === Summary ===
(newline)
(display "===================")
(newline)
(display "PASSED: ")
(display pass-count)
(newline)
(display "FAILED: ")
(display fail-count)
(newline)
(display "===================")
(newline)
