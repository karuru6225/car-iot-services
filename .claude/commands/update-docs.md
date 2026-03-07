Review the current state of the codebase and update the following documentation files to reflect reality:

1. **ARCHITECTURE.md** - Update if any of these changed:
   - Directory structure or file layout (`src/domain/`, `src/infra/`, `src/app/`)
   - Class names, struct names, or component responsibilities
   - Dependency rules between layers
   - Cloud infrastructure resources (Terraform)
   - Build system or toolchain

2. **CONTEXT.md** - Update if any of these changed:
   - S3/Glue schema (columns, types, partition structure)
   - MQTT payload format or fields
   - API endpoints, parameters, or response format
   - Pending tasks or known issues (mark completed items as done or remove them)
   - Hardware configuration

3. **README.md** - Update if any of these changed:
   - System architecture diagram
   - Hardware components
   - Repository structure
   - Deploy instructions or prerequisites

Read each file first, then compare against the actual code. Only update sections that are outdated or incorrect. Do not rewrite sections that are still accurate.
