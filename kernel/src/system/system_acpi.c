LIMINE_REQUEST
static volatile struct limine_rsdp_request system_acpi_rsdp_request = {
    .id       = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
};

typedef struct PACKED System_Acpi_Rsdp_Descriptor {
    U8  signature[8]; // "RSD PTR "
    U8  checksum;
    U8  oem_id[6];
    U8  revision;
    U32 rsdt_address;
} System_Acpi_Rsdp_Descriptor;

typedef struct PACKED System_Acpi_Xsdp_Descriptor {
    U8  signature[8]; // "RSD PTR "
    U8  checksum;
    U8  oem_id[6];
    U8  revision;     // 2
    U32 rsdt_address;

    U32 length;
    U64 xsdt_address;
    U8  extended_checksum;
    U8  reserved[3];
} System_Acpi_Xsdp_Descriptor;

typedef struct PACKED System_Acpi_Sdt_Header {
    U8  signature[4];
    U32 length;
    U8  revision;
    U8  checksum;
    U8  oem_id[6];
    U8  oem_table_id;
    U32 oem_revision;
    U32 creator_id;
    U32 creater_revision;
} System_Acpi_Sdt_Header;

internal bool system_acpi_validate(Bytes rsdp_data) {
    U32 result = 0;
    for (Int i = 0; i < rsdp_data.len; i++) {
        result += bytes_get(rsdp_data, i);
    }
    return (result & 0xFF) == 0;
}

internal bool system_acpi_sdt_validate(System_Acpi_Sdt_Header *sdt) {
    return system_acpi_validate((Bytes){ .ptr = (U8*)sdt, .len = (Int)sdt->length });
}

typedef struct System_Acpi {
    bool use_extended;
    union {
        System_Acpi_Rsdp_Descriptor *rsdp;
        System_Acpi_Xsdp_Descriptor *xsdp;
    };
    System_Acpi_Sdt_Header *sdt;
} System_Acpi;

global System_Acpi system_acpi;

internal void system_acpi_setup(void) {
    kassert(system_acpi_rsdp_request.revision == 0);
    kassert(system_acpi_rsdp_request.response != NULL);
    kassert(system_acpi_rsdp_request.response->revision == 0);
    kassert(system_hhdm_request.response != NULL);
    kassert(system_hhdm_request.response->revision == 0);

    printf("ACPI: got rsdp address %p\n", system_acpi_rsdp_request.response->address);

    System_Acpi_Rsdp_Descriptor *rsdp = system_acpi_rsdp_request.response->address;

    if (!system_acpi_validate(BYTES_FROM_PTR(rsdp))) {
        kpanic(STR("ACPI: rsdp checksum failed"));
    }

    System_Acpi_Xsdp_Descriptor *xsdp = NULL;
    if (rsdp->revision == 2) {
        xsdp = (System_Acpi_Xsdp_Descriptor *)rsdp;
        if (!system_acpi_validate(BYTES_FROM_PTR(xsdp))) {
            kpanic(STR("ACPI: xsdp checksum failed"));
        }
    }

    System_Acpi_Sdt_Header *sdt_address = (void *)((Uintptr)rsdp->rsdt_address + (Uintptr)system_hhdm_request.response->offset);
    if (xsdp) {
        sdt_address = (System_Acpi_Sdt_Header *)((Uintptr)xsdp->xsdt_address + (Uintptr)system_hhdm_request.response->offset);
        printf("ACPI: found xsdt at %p\n", sdt_address);
    } else {
        printf("ACPI: found rsdt at %p\n", sdt_address);
    }

    if (!system_acpi_sdt_validate(sdt_address)) {
        kpanic(STR("ACPI: rsdt checksum failed"));
    }

    system_acpi = (System_Acpi) {
        .use_extended = xsdp != NULL,
        .rsdp         = rsdp,
        .sdt          = sdt_address,
    };
}

internal Uintptr system_acpi_sdt_get(Int item) {
    kassert(system_acpi.rsdp != NULL);

    Int item_count = ((Int)system_acpi.sdt->length - size_of(System_Acpi_Sdt_Header)) / (system_acpi.use_extended ? 8 : 4);
    kassert(0 <= item && item < item_count);

    if (system_acpi.use_extended) {
        return (Uintptr)((U64 *)((Uintptr)system_acpi.sdt + (Uintptr)size_of(System_Acpi_Sdt_Header)))[item];
    } else {
        return (Uintptr)((U32 *)((Uintptr)system_acpi.sdt + (Uintptr)size_of(System_Acpi_Sdt_Header)))[item];
    }
}
