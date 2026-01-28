; Factorial test - the primary success criterion
(define fact
  (lambda (n)
    (if (< n 2)
        1
        (* n (fact (- n 1))))))

(display (fact 10))
(newline)
