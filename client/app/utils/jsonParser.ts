export function safeJsonParse(data: string) {
  try {
    return { parsed: JSON.parse(data), error: null };
  } catch (error) {
    console.error("Error parsing JSON:", error);
    return { parsed: null, error: "Failed to parse server response" };
  }
}
