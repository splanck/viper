# Property Patterns

This guide showcases idiomatic `PROPERTY` usages in BASIC OOP.

## Read-only property

```basic
CLASS Counter
  value AS INTEGER
  PROPERTY Current AS INTEGER
    GET
      RETURN value
    END GET
  END PROPERTY
END CLASS
```

## Write-only property

```basic
CLASS Sink
  PROPERTY Target AS STRING
    SET(s AS STRING)
      ' store or forward s
    END SET
  END PROPERTY
END CLASS
```

## Read-write with validation

```basic
CLASS Person
  name AS STRING
  PROPERTY Name AS STRING
    GET: RETURN name: END GET
    SET(s AS STRING)
      IF LEN(s) = 0 THEN
        EXIT SUB   ' ignore invalid value
      END IF
      name = s
    END SET
  END PROPERTY
END CLASS
```

## Caching in getter

```basic
CLASS Heavy
  cached AS STRING
  PROPERTY Payload AS STRING
    GET
      IF LEN(cached) = 0 THEN
        cached = "computed-once"
      END IF
      RETURN cached
    END GET
  END PROPERTY
END CLASS
```

## Static property

```basic
CLASS Settings
  STATIC PROPERTY Version AS INTEGER
    GET: RETURN 1: END GET
  END PROPERTY
END CLASS
```

