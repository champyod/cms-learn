# 0028 — Every external service is the deployer's configuration

Status: accepted 2026-11-12 · decision by the operator

## 1. The mistake this corrects

Asked which model the generation pipeline should call, the honest question was not "which one" but "who
chooses". This product is deployed by other people, for other people. Anything we pick becomes their
default, and a default chosen in our environment is a product that works where we work.

## 2. The decision

**No vendor, endpoint, model id, region or key of ours enters the product.**

| Fact | Who supplies it | What happens when it is missing |
|---|---|---|
| the model endpoint, model id and key | the deployer | the worker refuses to start and names every missing key |
| the database URL and the seal key | the deployer | the service refuses to start, as it already does |
| the object store endpoint, key and secret | the deployer | the store client refuses, as it already does |
| the mail server | the deployer | the mail switch stays off, as it already does |

The rule the code already follows for the database is now the rule for every external service: **read it from
configuration, refuse to start without it, and never print its value.**

## 3. What this forbids

- A default endpoint, model name or region compiled into the binary.
- A sample value in a tracked example file that would silently work: the example names the keys and leaves
  the values empty.
- A "sensible fallback" when a configured provider fails, because a fallback is a second vendor nobody chose.
- Any check that assumes a particular provider's response shape beyond the shape this repository defines.

## 4. Where the worker runs

A container in the stack, started on demand, with its own database role and its own copy of the deployer's
model configuration. It never holds a copy of the service's own credentials beyond what its role grants, and
its role cannot publish or verify, and the schema grants that a record rather than a convention.

## 5. Consequence

The pipeline cannot be demonstrated against a vendor we chose. Its tests therefore run against a **recorded
response and a stub client** for every non-model assertion, and the one end-to-end test that needs a real
model is skipped with a named reason when the deployer has not configured one — never silently passed.
