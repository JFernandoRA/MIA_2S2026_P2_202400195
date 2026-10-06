export function DiskMark({ size = 22 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 22 22" fill="none" aria-hidden="true">
      <circle cx="11" cy="11" r="9.5" stroke="currentColor" strokeWidth="1.4" />
      <circle cx="11" cy="11" r="5.5" stroke="currentColor" strokeWidth="1.4" />
      <circle cx="11" cy="11" r="1.6" fill="currentColor" />
    </svg>
  )
}

export function PartitionIcon({ size = 34 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 34 34" fill="none" aria-hidden="true">
      <rect x="3" y="6" width="28" height="22" rx="2" stroke="currentColor" strokeWidth="1.5" />
      <path d="M3 13h28M12 13v15" stroke="currentColor" strokeWidth="1.5" />
      <circle cx="26" cy="22" r="1.6" fill="currentColor" />
    </svg>
  )
}

export function FolderIcon({ size = 34 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 34 34" fill="none" aria-hidden="true">
      <path d="M3 9a2 2 0 0 1 2-2h8l3 3h13a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V9z" fill="currentColor" fillOpacity="0.18" stroke="currentColor" strokeWidth="1.5" />
    </svg>
  )
}

export function FileIcon({ size = 34 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 34 34" fill="none" aria-hidden="true">
      <path d="M8 3h12l7 7v19a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z" stroke="currentColor" strokeWidth="1.5" />
      <path d="M20 3v7h7M11 17h12M11 21h12M11 25h8" stroke="currentColor" strokeWidth="1.5" />
    </svg>
  )
}
