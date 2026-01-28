; ============================================
; Viper Lisp Demo — Language within a Language
; ============================================

; --- Factorial (recursive) ---
(define fact
  (lambda (n)
    (if (< n 2)
        1
        (* n (fact (- n 1))))))

(display "10! = ")
(display (fact 10))
(newline)

; --- Fibonacci ---
(define fib
  (lambda (n)
    (if (< n 2)
        n
        (+ (fib (- n 1)) (fib (- n 2))))))

(display "fib(20) = ")
(display (fib 20))
(newline)

; --- Higher-order functions ---
(define compose
  (lambda (f g)
    (lambda (x) (f (g x)))))

(define double (lambda (x) (* x 2)))
(define inc    (lambda (x) (+ x 1)))
(define double-then-inc (compose inc double))

(display "(inc (double 5)) = ")
(display (double-then-inc 5))
(newline)

; --- Closures: counter ---
(define make-counter
  (lambda (start)
    (begin
      (define n start)
      (lambda ()
        (begin
          (define prev n)
          (set! n (+ n 1))
          prev)))))

(define counter (make-counter 0))
(display "counter: ")
(display (counter))
(display " ")
(display (counter))
(display " ")
(display (counter))
(newline)

; --- Ackermann function ---
(define ack
  (lambda (m n)
    (cond
      ((= m 0) (+ n 1))
      ((= n 0) (ack (- m 1) 1))
      (else (ack (- m 1) (ack m (- n 1)))))))

(display "ack(3,4) = ")
(display (ack 3 4))
(newline)

; --- List processing ---
(define my-map
  (lambda (f lst)
    (if (null? lst)
        '()
        (cons (f (car lst))
              (my-map f (cdr lst))))))

(define squares (my-map (lambda (x) (* x x)) (list 1 2 3 4 5)))
(display "squares: ")

(define print-list
  (lambda (lst)
    (if (null? lst)
        (newline)
        (begin
          (display (car lst))
          (if (null? (cdr lst))
              (newline)
              (begin
                (display " ")
                (print-list (cdr lst))))))))

(print-list squares)

; --- FizzBuzz ---
(define fizzbuzz
  (lambda (n)
    (cond
      ((= (mod n 15) 0) (display "FizzBuzz "))
      ((= (mod n 3) 0)  (display "Fizz "))
      ((= (mod n 5) 0)  (display "Buzz "))
      (else (begin (display n) (display " "))))))

(display "FizzBuzz 1-20: ")
(define fb-loop
  (lambda (i max)
    (if (<= i max)
        (begin
          (fizzbuzz i)
          (fb-loop (+ i 1) max)))))
(fb-loop 1 20)
(newline)

; --- Church numerals ---
(define zero  (lambda (f) (lambda (x) x)))
(define succ  (lambda (n) (lambda (f) (lambda (x) (f ((n f) x))))))
(define church-add
  (lambda (m n)
    (lambda (f) (lambda (x) ((m f) ((n f) x))))))

(define church->int
  (lambda (n) ((n (lambda (x) (+ x 1))) 0)))

(define one   (succ zero))
(define two   (succ one))
(define three (succ two))
(define five  (church-add two three))

(display "Church 2+3 = ")
(display (church->int five))
(newline)

(display "Done!")
(newline)
