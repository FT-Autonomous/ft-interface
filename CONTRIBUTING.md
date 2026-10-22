# Contributing to ft-interface

Follow the [Linux kernel coding style](https://docs.kernel.org/process/coding-style.html) as a **starting point**, with a few conventions for our C++ firmware.

## Coding style

- Use tabs for indentation (8-column tab stops) and aim for lines under 80 characters.
- Use clear names: `PascalCase` for classes and structs; `snake_case` for functions and variables.
- Put opening braces on a **new line for classes, structs and functions**. For `if`, `for`, and `while`, keep the opening brace on the same line.
- Keep functions small and focused. Comment *why* something is done, not what obvious code does.

Example:

```cpp
struct ASSIOutput
{
	bool yellow_on;
	bool blue_on;
};

class ASSIController
{
public:
	ASSIOutput get_output() const;
};

void update_status()
{
	if (status_valid) {
		// Update outputs from a validated state.
	}
}
```

## Tests and changes

- Follow **test-driven development**: write a failing test, implement the behaviour, then refactor.
- Add or update tests when changing behaviour. Run the test suite before submitting a pull request.
- Use a descriptive branch name (for example, `feature/assi-logic`) and explain your changes in the pull request.

