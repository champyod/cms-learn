SELECT format('CREATE ROLE %I NOLOGIN', name)
FROM (VALUES ('cmslearn_app'), ('cmslearn_judge'), ('cmslearn_migrate'), ('cmslearn_backup'),
         ('cmslearn_generator'), ('cmslearn_certifier')) AS roles(name)
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = roles.name)
\gexec
